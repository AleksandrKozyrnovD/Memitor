#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "memory_read.h"


size_t read_memory(void *addr, void *buf, size_t nbytes)
{
    static const char *proc_path = "/proc/memitorreader";
    int fd = -1;
    pid_t pid;
    unsigned long a;
    char wbuf[128];
    ssize_t wlen;
    ssize_t written;
    ssize_t total = 0;

    if (!buf || nbytes == 0)
        return 0;

    fd = open(proc_path, O_WRONLY);
    if (fd < 0) {
        return 0;
    }

    pid = getpid();
    a = (unsigned long) addr;

    wlen = snprintf(wbuf, sizeof(wbuf), "%d %lx %zu", (int)pid, a, nbytes);
    if (wlen <= 0 || wlen >= (ssize_t)sizeof(wbuf)) {
        close(fd);
        return 0;
    }

    written = write(fd, wbuf, (size_t)wlen);
    if (written != wlen) {
        close(fd);
        return 0;
    }
    close(fd);

    fd = open(proc_path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    /* Read loop (handles short reads / EINTR) */
    while ((size_t)total < nbytes) {
        ssize_t r = read(fd, (char*)buf + total, nbytes - (size_t)total);
        if (r > 0) {
            total += r;
            /* If kernel returns fewer than requested, we stop when EOF/0 is seen */
            continue;
        } else if (r == 0) {
            /* EOF / no more data */
            break;
        } else {
            /* error */
            if (errno == EINTR)
                continue;
            total = 0;
            break;
        }
    }

    close(fd);
    return (size_t)total;
}
