#include "buddy_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>



void my_assert(int condition, const char* message){  
    if(!condition){
        printf("%s\n", message);
        assert(0);
    }  
}


// Действия со списками

void list_init(buddy_list_t* list){
    list->first = (buddy_node_t*)0;
    list->len = 0;
}

void list_add(buddy_list_t* list, buddy_node_t* node){
    node->next = list->first;
    node->prev = (buddy_node_t*)0;
    if(list->first)
        list->first->prev = node;
    list->first = node;
    list->len += 1;
}

void list_remove(buddy_list_t* list, buddy_node_t* node){
    assert(list->len > 0);
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

buddy_node_t* list_pop(buddy_list_t* list){
    buddy_node_t* res = list->first;
    if(res){
        list_remove(list, res);
    }
    return res;
}



// Сколько страниц нужно зарезервировать под служебные данные?
uint64_t get_serv_pages(int levels, uint64_t pgsize, uint64_t pages){
    uint64_t serv_size = levels * sizeof(buddy_list_t) + pages;
    return serv_size / pgsize + 1;
}

void* get_page_ptr(buddy_allocator_t* mem, int lvl, int number){
    return (char*)mem->data + (mem->pgsize << lvl) * number;
}



// Инициализация аллокатора

void init_state_table(buddy_allocator_t* mem){
    for(int i = 0; i < mem->pages; i++)
        mem->state_table[i] = BUDDY_FREE;
}

void init_lists(buddy_allocator_t* mem){
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

    assert(pages < (1 << lvl));

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
        assert(pages < (1 << lvl));
    }
}


void buddy_init(
    buddy_allocator_t* mem, 
    int levels, uint64_t pgsize,  // гиперпараметры 
    uint64_t pages, void* ptr     // распределяемые ресурсы
){
    assert(levels > 0);

    uint64_t serv_pages = get_serv_pages(levels, pgsize, pages);
    assert(serv_pages <= pages);

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

int buddy_log2(uint64_t n){
    for(int log = 0; log < sizeof(n) * 8; log++){
        if(n == (1 << log))
            return log;   
    }
    return -1;
}

int find_free_level(buddy_allocator_t* mem, int lvl){
    while(lvl < mem->levels){
        if(mem->lists[lvl].len > 0)
            return lvl;
        lvl += 1;
    }
    return -1;
}

void buddy_devide(buddy_allocator_t* mem, buddy_node_t* node, int initial_lvl, int final_lvl ){
    assert(initial_lvl >= final_lvl);
    while(initial_lvl > final_lvl){
        initial_lvl -= 1;

        // Вторую половину объявляем свободной, а первую продолжаем делить
        char* second_part = (char*)node + (mem->pgsize << initial_lvl);
        list_add( &mem->lists[initial_lvl], (buddy_node_t*)second_part);
    }
}

void* buddy_alloc(buddy_allocator_t* mem, uint64_t pages){
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
    assert(pages == 1 << lvl);

    // 1
    int free_lvl = find_free_level(mem, lvl);
    if(free_lvl == -1)
        return 0;
    assert(free_lvl >= lvl);

    // 2
    buddy_node_t* node = list_pop(&mem->lists[free_lvl]);
    assert(node != 0);

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


int get_page_number(buddy_allocator_t* mem, void* page_ptr){
    int d = (char*)page_ptr - (char*)mem->data;
    if(d % mem->pgsize != 0)
        return -1;
    if(!(0 <= d && d < mem->pages))
        return -1;
    return d / mem->pgsize;
}

void clear_state_table_part(buddy_allocator_t* mem, uint64_t pn){
    assert(mem->state_table[pn] >= 0);
    uint64_t size = 1 << mem->state_table[pn];
    for(int i = 0; i < size; i++){
        assert(mem->state_table[pn + i] != BUDDY_FREE);
        mem->state_table[pn + i] = BUDDY_FREE;
    }
}

void unite(buddy_allocator_t* mem, int lvl, void* free_block){

}

void buddy_free(buddy_allocator_t* mem, void* addr){
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
    assert(pn >= 0);

    // 1
    int lvl = mem->state_table[pn];
    assert(lvl >= 0);
    
    // 2
    clear_state_table_part(mem, pn);

    
}



