#include "buddy_alloc.h"


#ifdef XV6
    #include "kernel/riscv.h"
    #include "kernel/defs.h"
    static void my_assert(int condition, char* message){  
        if(!condition){
            panic(message);
        }  
    }
#else
    #include <stdio.h>
    #include <assert.h>
    static void my_assert(int condition, char* message){  
        if(!condition){
            printf("%s\n", message);
            assert(0);
        }  
    }
#endif



// Действия со списками

static void list_init(buddy_list_t* list){
    list->first = (buddy_node_t*)0;
    list->len = 0;
}

static void list_add(buddy_list_t* list, buddy_node_t* node){
    node->next = list->first;
    node->prev = (buddy_node_t*)0;
    if(list->first)
        list->first->prev = node;
    list->first = node;
    list->len += 1;
}

static void list_remove(buddy_list_t* list, buddy_node_t* node){
    my_assert(list->len > 0, "");
    buddy_node_t* prev = node->prev;
    buddy_node_t* next = node->next;
    if(prev && next){
        prev->next = next;
        next->prev = prev;
    }
    else if(!prev && next){
        list->first = next;
        next->prev = 0;
    }
    else if(prev && !next){
        prev->next = 0;
    }
    else {  // !prev && !next
        list->first = 0;
    }
    list->len--;   
}

static buddy_node_t* list_pop(buddy_list_t* list){
    buddy_node_t* res = list->first;
    if(res){
        list_remove(list, res);
    }
    return res;
}



// Сколько страниц нужно зарезервировать под служебные данные?
static uint64_t get_serv_pages(int levels, uint64_t pgsize, uint64_t pages){
    uint64_t serv_size = levels * sizeof(buddy_list_t) + pages;
    return serv_size / pgsize + 1;
}


// Инициализация аллокатора

static void init_state_table(buddy_allocator_t* mem){
    for(int i = 0; i < mem->pages; i++)
        mem->state_table[i] = BUDDY_FREE;
}

static void init_lists(buddy_allocator_t* mem){
    int lvl = mem->levels - 1;
    buddy_list_t* list = &mem->lists[lvl];
    list_init(list);
    char* curr_page = (char*)mem->data;
    uint64_t pages = mem->pages;    // количество оставшихся страниц

    // Изначально кусков верхнего уровня может быть много, с ними работаем отдельно
    uint64_t top_lvl_count = (pages >> lvl);
    for(int i = 0; i < top_lvl_count; i++){
        list_add(list, (buddy_node_t*)curr_page);
        curr_page += (1 << lvl) * mem->pgsize;
        pages -= (1 << lvl);
    }

    my_assert(pages < (1 << lvl), "");

    // Для остальных уровней не более одного куска
    while(lvl > 0){
        lvl -= 1;
        list -= 1;
        list_init(list);
        if(pages >= (1 << lvl)){
            list_add(list, (buddy_node_t*)curr_page);
            curr_page += (1 << lvl) * mem->pgsize;
            pages -= (1 << lvl);
        }
        my_assert(pages < (1 << lvl), "");
    }
}


void lib_buddy_init(
    buddy_allocator_t* mem, 
    int levels, uint64_t pgsize,  // гиперпараметры 
    uint64_t pages, void* ptr     // распределяемые ресурсы
){
    my_assert(levels > 0, "");

    uint64_t serv_pages = get_serv_pages(levels, pgsize, pages);
    my_assert(serv_pages <= pages, "");

    mem->levels = levels;
    mem->pgsize = pgsize;

    mem->lists = (buddy_list_t*)ptr;
    mem->state_table = (char*) ptr + sizeof(buddy_list_t) * levels;

    mem->pages = pages - serv_pages;
    mem->data = (char*) ptr + serv_pages * pgsize;

    init_state_table(mem);
    init_lists(mem);
}



// Выделение памяти

static int buddy_log2(uint64_t n){
    for(int log = 0; log < sizeof(n) * 8; log++){
        if(n == (1 << log))
            return log;   
    }
    return -1;
}

static int find_free_level(buddy_allocator_t* mem, int lvl){
    while(lvl < mem->levels){
        if(mem->lists[lvl].len > 0)
            return lvl;
        lvl += 1;
    }
    return -1;
}

static void buddy_devide(buddy_allocator_t* mem, buddy_node_t* node, int initial_lvl, int final_lvl ){
    my_assert(initial_lvl >= final_lvl, "");
    while(initial_lvl > final_lvl){
        initial_lvl -= 1;

        // Вторую половину объявляем свободной, а первую продолжаем делить
        char* second_part = (char*)node + (mem->pgsize << initial_lvl);
        list_add( &mem->lists[initial_lvl], (buddy_node_t*)second_part);
    }
}

void* lib_buddy_alloc(buddy_allocator_t* mem, uint64_t pages){
    /*
    0) Получаем по количеству страниц pages уровень куска (=log(pages)), который нужно выделить.
        Проверяем корректность запроса.
    1) Ищем свобоный кусок наименьшего уровня, чтобы нам хвавтило места.
        Делаем с помощью прохода по спискам - ищем список с ненулевой длиной
    2) Удаляем его из списка свободных
    3) Отделяем от этого куска кусок нужного размера.
        Свободный остаток состоит из нескольких кусков, добавляем их в списки.
    4) Проставляем байты в state_table
    5) Возвращаем соответствующий адрес  
    */

    // 0
    int lvl = buddy_log2(pages);
    if(lvl == -1)
        return 0;
    my_assert(pages == 1 << lvl, "");

    // 1
    int free_lvl = find_free_level(mem, lvl);
    if(free_lvl == -1)
        return 0;
    my_assert(free_lvl >= lvl, "");

    // 2
    buddy_node_t* node = list_pop(&mem->lists[free_lvl]);
    my_assert(node != 0, "");

    // 3
    buddy_devide(mem, node, free_lvl, lvl);

    // 4
    int first_page = ((char*)node - (char*)mem->data) / mem->pgsize;
    mem->state_table[first_page] = lvl;
    for(int i = 1; i < pages; i++){
        mem->state_table[first_page + i] = BUDDY_USED;
    }

    // 5
    return node;
}


// Освобождение памяти


static int get_page_number(buddy_allocator_t* mem, void* page_ptr){
    int d = (char*)page_ptr - (char*)mem->data;
    if(d % mem->pgsize != 0)
        return -1;
    int res = d / mem->pgsize;
    if(!(0 <= res && res < mem->pages))
        return -1;
    return res;
}


static void* get_page_ptr(buddy_allocator_t* mem, int page_number){
    return (char*)mem->data + mem->pgsize * page_number;
}

static void clear_state_table_part(buddy_allocator_t* mem, uint64_t pn){
    my_assert(mem->state_table[pn] >= 0, "");
    uint64_t size = 1 << mem->state_table[pn];
    for(int i = 0; i < size; i++){
        my_assert(mem->state_table[pn + i] != BUDDY_FREE, "");
        mem->state_table[pn + i] = BUDDY_FREE;
    }
}

static void add_free_block(buddy_allocator_t* mem, int lvl, uint64_t pn){
    /*
    До тех пор, пока сосед свободен, объединяемся с ним:
    1) Выкидываем соседа из списка
    2) Заменяем np, на min(np, npn)
    3) Повышаем уровень lvl
    В конце добавляем один большой кусок
    */
    my_assert(pn + (1 << lvl) <= mem->pages, "");  // кусок не должен вылезать за границы
    while(1){
        // 0
        my_assert((pn & ((1LL << lvl) - 1)) == 0, "");  // pn должен быть кратен 2 в степени lvl 
        uint64_t npn = pn ^ (1LL << lvl);  // neighbour page number
        if(npn + (1 << lvl) > mem->pages)
            break;
        if(mem->state_table[npn] != BUDDY_FREE)
            break;

        // 1
        buddy_node_t* n_node = get_page_ptr(mem, npn);
        list_remove(&mem->lists[lvl], n_node);

        // 2
        if(npn < pn)
            pn = npn;

        // 3
        lvl += 1;    
    }
    // В конце добавляем один большой кусок
    list_add(&mem->lists[lvl], get_page_ptr(mem, pn));
}


void lib_buddy_free(buddy_allocator_t* mem, void* addr){
    /*
    0) По адресу получаем корректный номер страницы, или понимаем что адрес
        неправильный
    1) Смотрим в таблицу состояний - если в ней по этому номеру значится начало
        выделенного куска, переходим дальше, иначе возвращаем ошибку (или паникуем)
    2) Очищаем кусок в таблице состояний
    3) Образовался свободный кусок. Склеиваем его (возможно нуль или несколько раз)
        и добавляем в список свободных участков
    */

    // 0
    int pn = get_page_number(mem, addr);
    my_assert(pn >= 0, "");  // паникуем

    // 1
    int lvl = mem->state_table[pn];
    my_assert(lvl >= 0, "");  // паникуем
    
    // 2
    clear_state_table_part(mem, pn);

    // 3
    add_free_block(mem, lvl, pn);

    
}



