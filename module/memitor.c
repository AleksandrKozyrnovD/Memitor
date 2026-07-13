#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/pagewalk.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/sched/mm.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aleksandr");
MODULE_DESCRIPTION("One module for monitoring heap memory usage");


/* ========================= Memory Reader ========================= */
#define MEM_READER_PROC_FILENAME "memitorreader"
#define MEM_READER_MAX_INPUT_SIZE 128
#define MEM_READER_MAX_OUTPUT 4096

static struct proc_dir_entry *mem_reader_proc_file;

static pid_t mem_reader_target_pid = -1;
static unsigned long mem_reader_target_addr = 0;
static size_t mem_reader_target_nbytes = 0;

static char mem_reader_output_buf[MEM_READER_MAX_OUTPUT];
static size_t mem_reader_output_size = 0;

static ssize_t mem_reader_write(struct file *file, const char __user *ubuf,
                                size_t count, loff_t *ppos)
{
    char kbuf[MEM_READER_MAX_INPUT_SIZE];

    if (count >= MEM_READER_MAX_INPUT_SIZE)
        return -EINVAL;

    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;
    kbuf[count] = '\0';

    if (sscanf(kbuf, "%d %lx %zu",
               &mem_reader_target_pid, &mem_reader_target_addr, &mem_reader_target_nbytes) != 3)
        return -EINVAL;

    if (mem_reader_target_nbytes > MEM_READER_MAX_OUTPUT)
        mem_reader_target_nbytes = MEM_READER_MAX_OUTPUT;

    pr_info("memitor: set pid=%d addr=0x%lx nbytes=%zu\n",
            mem_reader_target_pid, mem_reader_target_addr, mem_reader_target_nbytes);

    mem_reader_output_size = 0;
    return count;
}

static ssize_t mem_reader_read(struct file *file, char __user *ubuf,
                               size_t count, loff_t *ppos)
{
    struct task_struct *task;
    struct mm_struct *mm;
    size_t nread = 0;

    if (*ppos > 0 || mem_reader_target_pid < 0 || mem_reader_target_nbytes == 0)
        return 0;

    rcu_read_lock();
    task = pid_task(find_vpid(mem_reader_target_pid), PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    mm = get_task_mm(task);
    if (!mm) {
        put_task_struct(task);
        return -EFAULT;
    }

    nread = access_process_vm(task, mem_reader_target_addr,
                              mem_reader_output_buf, mem_reader_target_nbytes, 0);
    printk(KERN_INFO "memitor: read %zu bytes\n", nread);

    mmput(mm);
    put_task_struct(task);

    if (nread == 0)
        return -EFAULT;

    mem_reader_output_size = nread;

    if (copy_to_user(ubuf, mem_reader_output_buf, mem_reader_output_size))
        return -EFAULT;

    *ppos += mem_reader_output_size;
    return mem_reader_output_size;
}

static const struct proc_ops mem_reader_ops = {
    .proc_write = mem_reader_write,
    .proc_read  = mem_reader_read,
};

static int mem_reader_init(void)
{
    mem_reader_proc_file = proc_create(MEM_READER_PROC_FILENAME, 0666, NULL, &mem_reader_ops);
    if (!mem_reader_proc_file)
        return -ENOMEM;

    pr_info("memitorreader: memory reader loaded\n");
    return 0;
}

static void mem_reader_exit(void)
{
    remove_proc_entry(MEM_READER_PROC_FILENAME, NULL);
    pr_info("memitorreader: memory reader unloaded\n");
}


/* ===================== Memshot ===================== */
#define MEMSHOT_PROC_NAME "memshot"

static int (*walk_page_range_func)(struct vm_area_struct *vma,
                                  const struct mm_walk_ops *ops,
                                  void *private) = NULL;

static pid_t memshot_target_pid = 0;
static struct proc_dir_entry *memshot_proc_entry;


static void memshot_seq_set_overflow(struct seq_file *m)
{
    m->count = m->size;
}

void seq_put_hex_ll(struct seq_file *m, const char *delimiter,
                    unsigned long long v, unsigned int width)
{
    unsigned int len;
    int i;

    if (delimiter && delimiter[0]) {
        if (delimiter[1] == 0)
            seq_putc(m, delimiter[0]);
        else
            seq_puts(m, delimiter);
    }

    if (v == 0)
        len = 1;
    else
        len = (sizeof(v) * 8 - __builtin_clzll(v) + 3) / 4;

    if (len < width)
        len = width;

    if (m->count + len > m->size) {
        memshot_seq_set_overflow(m);
        return;
    }

    for (i = len - 1; i >= 0; i--) {
        m->buf[m->count + i] = hex_asc[0xf & v];
        v = v >> 4;
    }
    m->count += len;
}

static void memshot_seq_put_addr_range(struct seq_file *m,
                                      unsigned long start, unsigned long end)
{
    unsigned long pages = (end - start) / PAGE_SIZE;
    seq_setwidth(m, 32 + sizeof(void *) * 6 - 1);
    seq_putc(m, '\t');
    seq_put_hex_ll(m, NULL, start, 8);
    seq_put_hex_ll(m, "-", end, 8);
    seq_printf(m, " [%lu]", pages);
}

struct resident_collector {
    struct seq_file *m;
    unsigned long current_start;
    bool in_resident_range;
};

static int resident_pte_entry(pte_t *pte, unsigned long addr,
                              unsigned long next, struct mm_walk *walk)
{
    struct resident_collector *collector = walk->private;

    if (pte_present(*pte)) {
        if (!collector->in_resident_range) {
            collector->current_start = addr;
            collector->in_resident_range = true;
        }
    } else {
        if (collector->in_resident_range) {
            memshot_seq_put_addr_range(collector->m,
                                       collector->current_start, addr);
            seq_putc(collector->m, '\n');
            collector->in_resident_range = false;
        }
    }
    return 0;
}

static int resident_pte_hole(unsigned long addr, unsigned long next,
                             int depth, struct mm_walk *walk)
{
    struct resident_collector *collector = walk->private;

    if (collector->in_resident_range) {
        memshot_seq_put_addr_range(collector->m,
                                   collector->current_start, addr);
        seq_putc(collector->m, '\n');
        collector->in_resident_range = false;
    }
    return 0;
}

static const struct mm_walk_ops resident_walk_ops = {
    .pte_entry = resident_pte_entry,
    .pte_hole = resident_pte_hole,
    .walk_lock = PGWALK_RDLOCK,
};

static void process_vma_residency_memshot(struct seq_file *m,
                                          struct vm_area_struct *vma,
                                          struct mm_struct *mm)
{
    struct resident_collector collector = {
        .m = m,
        .current_start = 0,
        .in_resident_range = false
    };
    int ret = walk_page_range_func(vma, &resident_walk_ops, &collector);

    if (ret) {
        seq_puts(m, "   [walk_page_range failed]\n");
        return;
    }

    if (collector.in_resident_range) {
        memshot_seq_put_addr_range(m, collector.current_start, vma->vm_end);
        seq_putc(m, '\n');
    }

    if (!collector.in_resident_range && collector.current_start == 0)
        seq_puts(m, "\t[no resident pages]\n");
}

static void show_memshot_vma(struct seq_file *m,
                             struct vm_area_struct *vma,
                             struct mm_struct *mm)
{
    vm_flags_t flags = vma->vm_flags;
    unsigned long ino = 0;
    unsigned long long pgoff = 0;
    unsigned long start = vma->vm_start;
    unsigned long end = vma->vm_end;
    dev_t dev = 0;

    if (!(flags & VM_READ && flags & VM_WRITE))
        return;

    if (vma->vm_file) {
        const struct inode *inode = file_user_inode(vma->vm_file);
        dev = inode->i_sb->s_dev;
        ino = inode->i_ino;
        pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
    }

    unsigned long pages = (end - start) / PAGE_SIZE;
    seq_setwidth(m, 32 + sizeof(void *) * 6 - 1);
    seq_put_hex_ll(m, NULL, start, 8);
    seq_put_hex_ll(m, "-", end, 8);
    seq_printf(m, " [%lu]", pages);
    seq_putc(m, ' ');
    seq_putc(m, flags & VM_READ ? 'r' : '-');
    seq_putc(m, flags & VM_WRITE ? 'w' : '-');
    seq_putc(m, flags & VM_EXEC ? 'x' : '-');
    seq_putc(m, flags & VM_MAYSHARE ? 's' : 'p');
    seq_put_hex_ll(m, " ", pgoff, 8);
    seq_put_hex_ll(m, " ", MAJOR(dev), 2);
    seq_put_hex_ll(m, ":", MINOR(dev), 2);
    seq_put_decimal_ull(m, " ", ino);
    seq_putc(m, ' ');

    if (vma->vm_file) {
        seq_path(m, &vma->vm_file->f_path, "\n");
    } else if (vma->vm_mm && vma->vm_start <= vma->vm_mm->start_brk &&
               vma->vm_end >= vma->vm_mm->start_brk) {
        seq_puts(m, "[heap]");
    } else if (vma->vm_start <= vma->vm_mm->start_stack &&
               vma->vm_end >= vma->vm_mm->start_stack) {
        seq_puts(m, "[stack]");
    } else {
        seq_puts(m, "[anon]");
    }

    seq_putc(m, '\n');

    if (flags & VM_READ && flags & VM_WRITE)
        process_vma_residency_memshot(m, vma, mm);
}

struct memshot_iterator {
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    struct vma_iterator vmi;
};

static void *memshot_start(struct seq_file *m, loff_t *pos)
{
    struct memshot_iterator *it = m->private;
    struct task_struct *task;
    struct mm_struct *mm;

    if (memshot_target_pid == 0)
        return NULL;

    task = get_pid_task(find_get_pid(memshot_target_pid), PIDTYPE_PID);
    if (!task)
        return ERR_PTR(-ESRCH);

    it->task = task;
    mm = get_task_mm(task);
    if (!mm) {
        put_task_struct(task);
        it->task = NULL;
        return NULL;
    }

    it->mm = mm;

    if (mmap_read_lock_killable(mm)) {
        mmput(mm);
        put_task_struct(task);
        it->task = NULL;
        return ERR_PTR(-EINTR);
    }

    vma_iter_init(&it->vmi, mm, 0);
    it->vma = vma_next(&it->vmi);

    while (*pos && it->vma) {
        it->vma = vma_next(&it->vmi);
        (*pos)--;
    }

    return it->vma;
}

static void *memshot_next(struct seq_file *m, void *v, loff_t *pos)
{
    struct memshot_iterator *it = m->private;

    it->vma = vma_next(&it->vmi);
    (*pos)++;
    return it->vma;
}

static void memshot_stop(struct seq_file *m, void *v)
{
    struct memshot_iterator *it = m->private;
    if (!it->task)
        return;

    mmap_read_unlock(it->mm);
    mmput(it->mm);
    put_task_struct(it->task);
    it->task = NULL;
    it->mm = NULL;
    it->vma = NULL;
}

static int memshot_show(struct seq_file *m, void *v)
{
    struct memshot_iterator *it = m->private;
    if (!v || !it->mm)
        return 0;

    show_memshot_vma(m, v, it->mm);
    return 0;
}

static const struct seq_operations memshot_seq_ops = {
    .start = memshot_start,
    .next  = memshot_next,
    .stop  = memshot_stop,
    .show  = memshot_show,
};

static int memshot_open(struct inode *inode, struct file *file)
{
    int ret = seq_open_private(file, &memshot_seq_ops,
                               sizeof(struct memshot_iterator));
    if (ret)
        return ret;

    memset(((struct seq_file *)file->private_data)->private, 0,
           sizeof(struct memshot_iterator));
    return 0;
}

static int memshot_release(struct inode *inode, struct file *file)
{
    return seq_release_private(inode, file);
}

static ssize_t memshot_write(struct file *file, const char __user *ubuf,
                             size_t count, loff_t *ppos)
{
    char kbuf[32];
    int new_pid;

    printk(KERN_INFO "+memshot: write\n");

    if (count >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    if (kstrtoint(kbuf, 10, &new_pid))
        return -EINVAL;

    if (!pid_task(find_vpid(new_pid), PIDTYPE_PID))
        return -ESRCH;

    memshot_target_pid = new_pid;
    return count;
}

static const struct proc_ops memshot_fops = {
    .proc_open    = memshot_open,
    .proc_read    = seq_read,
    .proc_release = memshot_release,
    .proc_write   = memshot_write,
};

static int memshot_init(void)
{
    walk_page_range_func = (void *)0xffffffffa5e572b0;

    memshot_proc_entry = proc_create(MEMSHOT_PROC_NAME, 0666, NULL, &memshot_fops);
    if (!memshot_proc_entry)
        return -ENOMEM;

    pr_info("memshot: module loaded\n");
    return 0;
}

static void memshot_exit(void)
{
    remove_proc_entry(MEMSHOT_PROC_NAME, NULL);
    pr_info("memshot: module unloaded\n");
}

/* ----------------------- END MEMSHOT ------------------------- */



/* ===================== Memitor  ===================== */

#define MEMITOR_PROC_FILENAME "memitor"
#define MEMITOR_MAX_ENTRIES 1024
#define MEMITOR_MAX_ACCEPTABLE_DELTA (1ULL << 40)

struct mem_record {
    unsigned int timestamp_sec;
    unsigned int timestamp_usec;
    unsigned int seq;
    unsigned int brk_count;
    unsigned int mmap_count;
    unsigned long resident_pages;
};

static int memitor_pid_set = 0;
static int memitor_pid = -1;
static struct task_struct *memitor_target_task = NULL;

static struct mem_record *memitor_records;
static unsigned int memitor_rec_head = 0;
static unsigned int memitor_rec_count = 0;
static unsigned int memitor_seq_counter = 0;

static unsigned int memitor_brk_count = 0;
static unsigned int memitor_mmap_count = 0;

static spinlock_t memitor_records_lock;

static struct proc_dir_entry *memitor_proc_entry;

static void memitor_append_record_locked(struct mm_struct *mm)
{
    unsigned long flags;
    unsigned long resident = mm ? get_mm_rss(mm) : 0;

    spin_lock_irqsave(&memitor_records_lock, flags);

    if (!memitor_records) {
        spin_unlock_irqrestore(&memitor_records_lock, flags);
        return;
    }

    memitor_seq_counter++;
    ktime_t kt = ktime_get();
    u64 nsec = ktime_to_ns(kt);
    memitor_records[memitor_rec_head].timestamp_sec =
        (unsigned int)(nsec / NSEC_PER_SEC);
    memitor_records[memitor_rec_head].timestamp_usec =
        (unsigned int)((nsec % NSEC_PER_SEC) / NSEC_PER_USEC);

    memitor_records[memitor_rec_head].seq = memitor_seq_counter;
    memitor_records[memitor_rec_head].brk_count = memitor_brk_count;
    memitor_records[memitor_rec_head].mmap_count = memitor_mmap_count;
    memitor_records[memitor_rec_head].resident_pages = resident;

    memitor_rec_head = (memitor_rec_head + 1) % MEMITOR_MAX_ENTRIES;
    if (memitor_rec_count < MEMITOR_MAX_ENTRIES)
        memitor_rec_count++;

    spin_unlock_irqrestore(&memitor_records_lock, flags);
}

static void memitor_reset_for_new_pid(void)
{
    unsigned long flags;
    spin_lock_irqsave(&memitor_records_lock, flags);

    memitor_brk_count = 0;
    memitor_mmap_count = 0;
    memitor_seq_counter = 0;
    memitor_rec_head = 0;
    memitor_rec_count = 0;

    if (memitor_records)
        memset(memitor_records, 0,
               sizeof(struct mem_record) * MEMITOR_MAX_ENTRIES);

    spin_unlock_irqrestore(&memitor_records_lock, flags);
}

static int memitor_pre_brk(struct kprobe *p, struct pt_regs *regs)
{
    struct task_struct *cur = current;
    struct mm_struct *mm;

    if (memitor_pid_set && memitor_target_task &&
        cur->pid != memitor_pid)
        return 0;

    mm = cur->mm;

    printk(KERN_INFO "+memitor: brk: [%d] rax=%lx rbx=%lx rdi=%lx rsi=%lx rdx=%lx rcx=%lx\n",
           memitor_pid, regs->ax, regs->bx, regs->di,
           regs->si, regs->dx, regs->cx);

    memitor_brk_count++;
    memitor_append_record_locked(mm);
    return 0;
}

static int memitor_pre_mmap(struct kprobe *p, struct pt_regs *regs)
{
    struct task_struct *cur = current;
    struct mm_struct *mm;

    if (memitor_pid_set && memitor_target_task &&
        cur->pid != memitor_pid)
        return 0;

    mm = cur->mm;

    printk(KERN_INFO "+memitor: mmap: [%d] rax=%lx rbx=%lx rdi=%lx rsi=%lx rdx=%lx rcx=%lx\n",
           memitor_pid, regs->ax, regs->bx, regs->di,
           regs->si, regs->dx, regs->cx);

    memitor_mmap_count++;
    memitor_append_record_locked(mm);
    return 0;
}

static struct kprobe memitor_kp_brk = {
    .symbol_name = "__x64_sys_brk",
    .pre_handler = memitor_pre_brk
};

static struct kprobe memitor_kp_mmap = {
    .pre_handler = memitor_pre_mmap
};

static ssize_t memitor_proc_read(struct file *file, char __user *buf,
                                 size_t count, loff_t *ppos)
{
    char *kbuf;
    size_t len = 0, max_len, line_len;
    unsigned long flags;
    unsigned int i, idx;
    int ret;

    if (*ppos > 0)
        return 0;

    if (!memitor_target_task) {
        kbuf = kmalloc(256, GFP_KERNEL);
        if (!kbuf)
            return -ENOMEM;

        len = scnprintf(kbuf, 256,
                        "PID: (none set), records: %u\n",
                        memitor_rec_count);

        if (copy_to_user(buf, kbuf, len))
            ret = -EFAULT;
        else ret = len;

        kfree(kbuf);
        if (ret >= 0)
            *ppos = len;
        return ret;
    }

    max_len = memitor_rec_count * 140 + 512;
    kbuf = kmalloc(max_len, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    len += scnprintf(kbuf + len, max_len - len,
                     "PID: %d, records: %u\n",
                     memitor_target_task->pid,
                     memitor_rec_count);

    len += scnprintf(kbuf + len, max_len - len,
                     "columns: timestamp seq brk_count mmap_count resident_pages\n");

    spin_lock_irqsave(&memitor_records_lock, flags);

    idx = (memitor_rec_head + MEMITOR_MAX_ENTRIES - memitor_rec_count) %
          MEMITOR_MAX_ENTRIES;

    for (i = 0; i < memitor_rec_count; i++) {
        struct mem_record *r = &memitor_records[idx];

        line_len = scnprintf(kbuf + len, max_len - len,
            "%u.%06u %llu %u %u %lu\n",
            r->timestamp_sec,
            r->timestamp_usec,
            (unsigned long long)r->seq,
            r->brk_count,
            r->mmap_count,
            r->resident_pages);

        len += line_len;

        idx = (idx + 1) % MEMITOR_MAX_ENTRIES;

        if (len + 300 >= max_len)
            break;
    }

    spin_unlock_irqrestore(&memitor_records_lock, flags);

    if (len > count)
        len = count;

    ret = copy_to_user(buf, kbuf, len) ? -EFAULT : len;
    kfree(kbuf);

    if (ret >= 0)
        *ppos = len;

    return ret;
}

static ssize_t memitor_proc_write(struct file *file,
                                  const char __user *ubuf,
                                  size_t count, loff_t *ppos)
{
    char kbuf[32];
    int new_pid;
    struct task_struct *t;

    if (count >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    if (kstrtoint(kbuf, 10, &new_pid))
        return -EINVAL;

    t = pid_task(find_vpid(new_pid), PIDTYPE_PID);
    if (!t)
        return -ESRCH;

    memitor_pid = new_pid;
    memitor_pid_set = 1;
    memitor_target_task = t;

    memitor_reset_for_new_pid();
    return count;
}

static int memitor_proc_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int memitor_proc_release(struct inode *inode, struct file *file)
{
    return 0;
}

static const struct proc_ops memitor_fops = {
    .proc_open = memitor_proc_open,
    .proc_release = memitor_proc_release,
    .proc_read = memitor_proc_read,
    .proc_write = memitor_proc_write
};

static int memitor_init(void)
{
    int ret;

    spin_lock_init(&memitor_records_lock);

    memitor_records = kzalloc(sizeof(struct mem_record) *
                             MEMITOR_MAX_ENTRIES, GFP_KERNEL);
    if (!memitor_records)
        return -ENOMEM;

    ret = register_kprobe(&memitor_kp_brk);
    if (ret < 0)
        goto err_free;

    memitor_kp_mmap.symbol_name = "do_mmap_pgoff";
    ret = register_kprobe(&memitor_kp_mmap);

    if (ret < 0) {
        memitor_kp_mmap.symbol_name = "do_mmap";
        ret = register_kprobe(&memitor_kp_mmap);

        if (ret < 0)
            goto err_unreg_brk;
    }

    memitor_proc_entry = proc_create(MEMITOR_PROC_FILENAME,
                                    0666, NULL, &memitor_fops);
    if (!memitor_proc_entry) {
        ret = -ENOMEM;
        goto err_unreg_mmap;
    }

    pr_info("memitor: module loaded\n");
    return 0;

err_unreg_mmap:
    unregister_kprobe(&memitor_kp_mmap);
err_unreg_brk:
    unregister_kprobe(&memitor_kp_brk);
err_free:
    kfree(memitor_records);
    memitor_records = NULL;
    return ret;
}

static void memitor_exit(void)
{
    remove_proc_entry(MEMITOR_PROC_FILENAME, NULL);
    unregister_kprobe(&memitor_kp_mmap);
    unregister_kprobe(&memitor_kp_brk);

    if (memitor_records)
        kfree(memitor_records);

    pr_info("memitor: module unloaded\n");
}


/* =============== INIT/EXIT =============== */

static int __init unified_init(void)
{
    int ret;

    if ((ret = mem_reader_init()) != 0)
        return ret;
    if ((ret = memshot_init()) != 0)
    {
        mem_reader_exit(); return ret;
    }
    if ((ret = memitor_init()) != 0)
    {
        memshot_exit();
        mem_reader_exit();
        return ret;
    }

    pr_info("Unified module loaded successfully.\n");
    return 0;
}

static void __exit unified_exit(void)
{
    memitor_exit();
    memshot_exit();
    mem_reader_exit();

    pr_info("Unified module unloaded.\n");
}

module_init(unified_init);
module_exit(unified_exit);
