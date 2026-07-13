#include "AVL.h"
#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

#include "mallochook.h"

//Файл логов
#define LOG_FILE "./mallochook.log"
FILE *log_file = NULL;

//Выделенные и свободные указатели
struct ptr_node *allocated_ptrs = NULL;
struct ptr_node *freed_ptrs = NULL;

int allocated_ptrs_count = 0;
int freed_ptrs_count = 0;

size_t total_bytes_requested = 0;

int log_switch = 1;

#define ADD_PTR(root, ptr) { \
    if (log_switch) { \
        log_switch = 0; \
        root = Insert(root, ptr); \
        log_switch = 1; \
    } \
    else { \
        root = Insert(root, ptr); \
    } \
} \

#define DEL_PTR(root, ptr) { \
    if (log_switch) { \
        log_switch = 0; \
        root = Delete(root, ptr); \
        log_switch = 1; \
    } \
    else { \
        root = Delete(root, ptr); \
    } \
} \


// Типы для оригинальных функций
typedef void* (*malloc_func_t)(size_t);
typedef void* (*realloc_func_t)(void*, size_t);
typedef void* (*calloc_func_t)(size_t, size_t);
typedef void (*free_func_t)(void*);

// Глобальные указатели на оригинальные функции
static malloc_func_t orig_malloc = NULL;
static realloc_func_t orig_realloc = NULL;
static calloc_func_t orig_calloc = NULL;
static free_func_t orig_free = NULL;

// Флаг инициализации
static int initialized = 0;

// Ваши callback-функции
void before_malloc(size_t size) {
    // if (log_switch)
    //     fprintf(log_file, "[BEFORE] malloc(%zu)\n", size);
}

void after_malloc(void* ptr, size_t size) {
    if (log_switch)
    {
        fprintf(log_file, "[AFTER] malloc -> %p (%zu bytes)\n", ptr, size);
        // ADD_PTR(allocated_ptrs, ptr);
        total_bytes_requested += size;
        allocated_ptrs_count++;
    }
}

void before_realloc(void* ptr, size_t new_size) {
    if (log_switch)
    {
        fprintf(log_file, "[BEFORE] realloc(%p, %zu)\n", ptr, new_size);
    }
}

void after_realloc(void* old_ptr, void* new_ptr, size_t new_size) {
    if (log_switch)
    {
        fprintf(log_file, "[AFTER] realloc %p -> %p (%zu bytes)\n", old_ptr, new_ptr, new_size);
        // DEL_PTR(allocated_ptrs, old_ptr);
        // ADD_PTR(allocated_ptrs, new_ptr);
        allocated_ptrs_count++;
        total_bytes_requested += new_size; //очень криво. нет учета старого размера
    }
}

void before_calloc(size_t num, size_t size) {
    // if (log_switch)
    // {
    //     fprintf(log_file, "[BEFORE] calloc(%zu, %zu)\n", num, size);
    // }
}

void after_calloc(void* ptr, size_t num, size_t size) {
    if (log_switch)
    {
        fprintf(log_file, "[AFTER] calloc -> %p (%zu elements of %zu bytes)\n", ptr, num, size);
        // ADD_PTR(allocated_ptrs, ptr);
        total_bytes_requested += num * size;
        allocated_ptrs_count++;
    }
}

void before_free(void* ptr) {
    // if (log_switch)
    //     fprintf(log_file, "[BEFORE] free(%p)\n", ptr);
}

void after_free(void* ptr) {
    if (log_switch)
    {
        fprintf(log_file, "[AFTER] free(%p)\n", ptr);
        // DEL_PTR(allocated_ptrs, ptr);
        // ADD_PTR(freed_ptrs, ptr);
        freed_ptrs_count++;
    }
}

// Функция инициализации, которая выполняется автоматически
static void __attribute__((constructor)) init_hooks(void) {
    
    // Получаем указатели на оригинальные функции
    orig_malloc = (malloc_func_t)dlsym(RTLD_NEXT, "malloc");
    orig_realloc = (realloc_func_t)dlsym(RTLD_NEXT, "realloc");
    orig_calloc = (calloc_func_t)dlsym(RTLD_NEXT, "calloc");
    orig_free = (free_func_t)dlsym(RTLD_NEXT, "free");
    
    log_file = fopen(LOG_FILE, "w");
    
    
    if (!orig_malloc || !orig_realloc || !orig_calloc || !orig_free || !log_file) {
        fprintf(stderr, "Error: Failed to get original functions OR open log file\n");
        _exit(1);
    }
    fprintf(log_file, "=== Memory hooks initialized ===\n");
    
    initialized = 1;
    fprintf(log_file, "Original functions captured successfully\n");
}

// Функция очистки, которая выполняется при выгрузке библиотеки
static void __attribute__((destructor)) cleanup_hooks(void) {
    fprintf(log_file, "=== Memory hooks unloaded ===\n");
    fprintf(log_file, " -> Total bytes requested: %zu\n", total_bytes_requested);
    fprintf(log_file, " -> Allocated pointers: %d\n", allocated_ptrs_count);
    fprintf(log_file, " -> Freed pointers: %d\n", freed_ptrs_count);

    // fclose(log_file);
}

// Перехваченные функции
void* malloc(size_t size) {
    if (!initialized) {
        // На случай, если malloc вызван до инициализации
        return ((malloc_func_t)dlsym(RTLD_NEXT, "malloc"))(size);
    }
    
    before_malloc(size);
    void* ptr = orig_malloc(size);
    after_malloc(ptr, size);
    
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (!initialized) {
        return ((realloc_func_t)dlsym(RTLD_NEXT, "realloc"))(ptr, size);
    }
    
    before_realloc(ptr, size);
    void* new_ptr = orig_realloc(ptr, size);
    after_realloc(ptr, new_ptr, size);
    
    return new_ptr;
}

void* calloc(size_t num, size_t size) {
    if (!initialized) {
        return ((calloc_func_t)dlsym(RTLD_NEXT, "calloc"))(num, size);
    }
    
    before_calloc(num, size);
    void* ptr = orig_calloc(num, size);
    after_calloc(ptr, num, size);
    
    return ptr;
}

void free(void* ptr) {
    if (!initialized) {
        ((free_func_t)dlsym(RTLD_NEXT, "free"))(ptr);
        return;
    }
    
    before_free(ptr);
    orig_free(ptr);
    after_free(ptr);
}
