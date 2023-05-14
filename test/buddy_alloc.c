#include "buddy_alloc.h"
#include <stdio.h>
#include <stdlib.h>

int test(){
    return 1;
}


void assert(int condition, const char* message){  
    if(!condition){
        printf("%s\n", message);
        exit(-1);
    }  
}



void list_init(buddy_list_t* list){
    list->first = (buddy_node_t*)0;
    list->len = 0;
}

void list_add(buddy_list_t* list, buddy_node_t* node){
    node->next = list->first;
    list->first = node;
    list->len += 1;
}

buddy_node_t* list_pop(buddy_list_t* list){
    buddy_node_t* res = list->first;
    if(res)
        list->first = list->first->next;
    return res;
}




uint64_t get_serv_pages(int levels, uint64_t pgsize, uint64_t pages){
    uint64_t serv_size = levels * sizeof(buddy_list_t) + pages;
    return serv_size / pgsize + 1;
}

void* get_page_ptr(buddy_allocator_t* mem, int lvl, int number){
    return (char*)mem->data + (mem->pgsize << lvl) * number;
}



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

    assert(pages < (1 << lvl), "");

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
        assert(pages < (1 << lvl), "");
    }
}



void buddy_init(
    buddy_allocator_t* mem, 
    int levels, uint64_t pgsize,  // гиперпараметры 
    uint64_t pages, void* ptr     // распределяемые ресурсы
){
    assert(levels > 0, "");

    uint64_t serv_pages = get_serv_pages(levels, pgsize, pages);
    assert(serv_pages <= pages, "");

    mem->levels = levels;
    mem->pgsize = pgsize;

    mem->lists = (buddy_list_t*)ptr;
    mem->state_table = (char*) ptr + sizeof(buddy_list_t) * levels;

    mem->pages = pages - serv_pages;
    mem->data = (char*) ptr + serv_pages * pgsize;

    init_state_table(mem);

 
}