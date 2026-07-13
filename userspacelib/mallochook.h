#ifndef __MALLOCHOOK_H__
#define __MALLOCHOOK_H__

#include <stdio.h>

#define __AVL_IMPL__
#include "AVL.h"


//NULL, если не инициализированы
//Выделенные указатели
extern struct ptr_node *allocated_ptrs;
extern int allocated_ptrs_count;

//NULL, если не инициализированы
//Свободные указатели
extern struct ptr_node *freed_ptrs;
extern int freed_ptrs_count;

//Общее запрошенное у ОС количество байт виртуальной памяти
extern size_t total_bytes_requested;

//Файл логов
extern FILE *log_file;


//Отключение/включение логирования
//По умолчанию логирование включено
//Так как происходит логирование выделение памяти,
//мы не должны логировать выделение памяти под служебные структуры 
extern int log_switch;

#endif
