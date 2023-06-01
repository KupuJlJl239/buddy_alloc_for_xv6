#include "slab_alloc.h"


// Функция для аварийного завершения в случае ошибки пользователя
static void my_assert(int condition, char* message);

// ASSERT(condition) - это макрос для проверки инвариантов внутри алгоритма


#ifdef XV6
    #include "kernel/riscv.h"
    #include "kernel/defs.h"
    static void my_assert(int condition, char* message){  
        if(!condition){
            panic(message);
        }  
    }
    #define ASSERT(condition) do{\
        if(!(condition)){\
            printf("assertion FAILED on line %d\n", __LINE__);\
            panic("buddy allocation\n");\
        }\
    }while(0)
    
#else
    #include <stdio.h>
    #include <assert.h>
    static void my_assert(int condition, char* message){  
        if(!condition){
            printf("%s\n", message);
            assert(0);
        }  
    }
    #define ASSERT(condition) do{\
        if(!(condition)){\
            printf("assertion FAILED on line %d\n", __LINE__);\
            assert(0);\
        }\
    }while(0)
#endif



////////////////////////////////
///   Действия со списками   ///
////////////////////////////////

typedef struct slab_page{
    struct slab_page* next;
    struct slab_page* prev;
    struct slab_list* list;
    uint used_cells;
} slab_page_t;

typedef struct slab_list{
    slab_page_t head;
    uint len;
} slab_list_t;

static void list_init(slab_list_t* list, uint used_cells){
    list->head.prev = 0,
    list->head.next = 0,
    list->head.list = list;
    list->head.used_cells = used_cells;
    list->len = 0;
}

static void list_insert(slab_page_t* base_block, slab_page_t* new_block){
    slab_list_t* list = base_block->list;

    ASSERT(list->head.used_cells == base_block->used_cells);
    new_block->used_cells = list->head.used_cells;
    new_block->list = list;

    new_block->prev = base_block;
    new_block->next = base_block->next;

    base_block->next = new_block;

    list->len += 1;
}

static void list_remove(slab_page_t* block){
    slab_list_t* list = block->list;
    ASSERT(block->prev != 0);
    ASSERT(list->len > 0);

    slab_page_t* prev = block->prev;
    slab_page_t* next = block->next;

    prev->next = next;
    if(next)
        next->prev = prev;

    list->len -= 1;      
}

static void list_add(slab_list_t* list, slab_page_t* new_block){
    list_insert(&list->head, new_block);
}


////////////////////////////////////
///   Инициализация аллокатора   ///
////////////////////////////////////


static uint64 serv_pages(uint64 pgsize, uint64 n){
    uint64 res = 1;
    while(1){
        if(res * pgsize >= n)
            return res;
        res *= 2;
    }
}

static void init_lists(slab_alloc_t* slab){
    uint64 lists_size = sizeof(slab_list_t) * slab->cells;
    uint64 serv = serv_pages(slab->pgsize, lists_size);
    slab->lists = slab->buddy_alloc(serv); 
    ASSERT(slab->lists != 0);
    for(int i = 0; i < slab->cells; i++){
        list_init(&slab->lists[i], i);
    }
}

void lib_slab_init(
    slab_alloc_t* slab,
    uint pgsize,
    uint ssize,
    void* (*buddy_alloc)(uint64),
    void (*buddy_free)(void*),
    void* (*pgbegin)(void*)
){
    slab->pgsize = pgsize;
    slab->ssize = ssize;
    slab->buddy_alloc = buddy_alloc;
    slab->buddy_free = buddy_free;
    slab->pgbegin = pgbegin;
    slab->cells = (pgsize - sizeof(slab_page_t)) / (1 + ssize);

    init_lists(slab);
}


////////////////////////////
///   Выделение памяти   ///
//////////////////////////// 


char* bitmaps_ptr(slab_alloc_t* slab, slab_page_t* page){
    return (char*)page + sizeof(slab_page_t);
}

void* cells_ptr(slab_alloc_t* slab, slab_page_t* page){
    return (char*)page + sizeof(slab_page_t) + slab->cells;
}

slab_page_t* new_page(slab_alloc_t* slab){
    slab_page_t* page = slab->buddy_alloc(1);
    ASSERT(page != 0);
    char* bitmaps = bitmaps_ptr(slab, page);
    for(int i = 0; i < slab->cells; i++){
        bitmaps[i] = 0;
    }
    ASSERT(bitmaps[0] == 0);
    list_add(&slab->lists[0], page);
    ASSERT(bitmaps_ptr(slab, page) == bitmaps);
    ASSERT(bitmaps[0] == 0);
    ASSERT(page->used_cells == 0);
    ASSERT(page->list == &slab->lists[0]);
    return page;
}

void* page_alloc_cell(slab_alloc_t* slab, slab_page_t* page){
    ASSERT(page->used_cells < slab->cells);

    char* bitmaps = bitmaps_ptr(slab, page);
    uint i = 0;
    while(bitmaps[i] == 1){
        ASSERT(i < page->used_cells);
        i++;     
    }

    ASSERT(bitmaps[i] == 0);
    bitmaps[i] = 1;

    list_remove(page);
    list_add(&slab->lists[page->used_cells + 1], page);

    return (char*)cells_ptr(slab, page) + i * slab->ssize;
}


void* lib_slab_alloc(slab_alloc_t* slab){
    int cells = slab->cells - 2;
    while(cells >= 0){
        slab_list_t* list = &slab->lists[cells];
        if(list->len > 0){
            slab_page_t* page = list->head.next;
            return page_alloc_cell(slab, page);
        }
        cells -= 1;
    }

    ASSERT(cells == -1);
    slab_page_t* page = new_page(slab);
    char* bitmaps = bitmaps_ptr(slab, page);
    return page_alloc_cell(slab, page);
}


///////////////////////////////
///   Освобождение памяти   ///
///////////////////////////////


void page_clean_cell(slab_alloc_t* slab, slab_page_t* page, void* ptr){
    char* bitmaps = bitmaps_ptr(slab, page);
    void* cells = cells_ptr(slab, page);
    int i = ((char*)ptr - (char*)cells) / slab->ssize;
    ASSERT(bitmaps[i] == 1);
    bitmaps[i] = 0;
}

/*
NB!   Корректность адреса не проверяется. 
Но это и не нужно, ведь код в ядре мы сами и пишем.
*/
void lib_slab_free(slab_alloc_t* slab, void* ptr){
    slab_page_t* page = slab->pgbegin(ptr);
    list_remove(page);
    list_add(&slab->lists[page->used_cells - 1], page);
    page_clean_cell(slab, page, ptr);
}