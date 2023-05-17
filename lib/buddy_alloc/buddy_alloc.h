#pragma once
#include "inttypes.h"


enum BuddyTableState{
    BUDDY_FREE = -1,
    BUDDY_USED = -2,
};




/*
Все свободные куски одного размера соединены в связный список.
Итого есть по одному списку для каждого размера.
В начале каждого свободного куска лежит узел списка.
Свободные куски в каждом списке расположены не обязательно по порядку.
Также список хранит свою длину.
*/
typedef struct buddy_node{
    struct buddy_node* next;
} buddy_node_t;

typedef struct {
    buddy_node_t* first;
    uint64_t len;
} buddy_list_t;



/*
В первых нескольких страницах хранятся метаданные:
    - списки свободных кусков (поле lists)
    - таблица состояний (поле state_table)
Остальные страницы рабочие.

Общая логика такая: для выделений и быстрого поиска свободных
кусков используем списки lists, а для освобождений и проверки
состояния используем state_table. 
В результате все операции работают за константу.

Таблица состояний позволяет по данному номеру страницы 
сразу определить, что в ней.
У первой рабочей страницы номер 0.
Пусть номер данной страницы равен n.
Тогда есть такие варианты: 
    1) state_table[n] == -1   =>  эта страница свободна, 
    2) state_table[n] == -2  =>  эта страница занята, но не является
        первой в каком-то выделенном куске
    3) state_table[n] == lvl >= 0  =>  эта страница занята и является
        первой в выделенном куске уровня lvl (и размера 1 << lvl страниц)
*/
typedef struct {
    int levels;
    uint64_t pgsize;

    buddy_list_t* lists;  // указывает также на начало метаданных
    char* state_table;

    uint64_t pages;  // количество рабочих страниц
    void* data;      // указатель на первую рабочую страницу
} buddy_allocator_t;





void buddy_init(
    buddy_allocator_t* mem, 
    int levels, uint64_t pgsize,  // гиперпараметры 
    uint64_t pages, void* ptr     // распределяемые ресурсы
);

void* buddy_alloc(buddy_allocator_t* mem, uint64_t pages);
void buddy_free(buddy_allocator_t* mem, void* addr);



// typedef struct {
//     uint64_t total;
//     uint64_t free;
//     uint64_t free_by_size[BUDDY_LEVELS];
// } buddy_info_t;

// void buddy_info(buddy_allocator_t* mem, uint64_t* total, uint64_t* free, uint64_t free_by_size);