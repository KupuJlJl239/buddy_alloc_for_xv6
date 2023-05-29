#include "buddy_alloc.h"


// Удобная функция для отлова ошибок или аварийного завершения
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


////////////////////////////////
///   Действия со списками   ///
////////////////////////////////

static void list_init(buddy_list_t* list, int level){
    list->head.prev = 0,
    list->head.next = 0,
    list->head.list = list;
    list->head.level = level;
    list->len = 0;
}

static void insert_block(buddy_free_block_t* base_block, buddy_free_block_t* new_block){
    buddy_list_t* list = base_block->list;

    my_assert(list->head.level == base_block->level, "");
    new_block->level = list->head.level;
    new_block->list = list;

    new_block->prev = base_block;
    new_block->next = base_block->next;

    base_block->next = new_block;

    list->len += 1;
}

static void remove_block(buddy_free_block_t* block){
    buddy_list_t* list = block->list;
    my_assert(block->prev != 0, "");
    my_assert(list->len > 0, "");

    buddy_free_block_t* prev = block->prev;
    buddy_free_block_t* next = block->next;

    prev->next = next;
    if(next)
        next->prev = prev;

    list->len -= 1;      
}

static void list_add(buddy_list_t* list, buddy_free_block_t* new_block){
    insert_block(&list->head, new_block);
}



//////////////////////////////////////////////////
///   Преобразование адресов и номеров страниц ///
//////////////////////////////////////////////////

// Преобразует указатель на страницу в её номер. Если указатель не соответствует существующей странице, возвращает -1.
static int get_page_number(buddy_allocator_t* mem, void* page_ptr){
    int d = (char*)page_ptr - (char*)mem->data;
    if(d % mem->pgsize != 0)
        return -1;
    int res = d / mem->pgsize;
    if(!(0 <= res && res < mem->pages))
        return -1;
    return res;
}

// По номеру страницы возвращает указатель на неё. Возвращает нулевой указатель, если номер некорректен.
static void* get_page_ptr(buddy_allocator_t* mem, int page_number){
    if(!(0 <= page_number && page_number < mem->pages))
        return 0;
    return (char*)mem->data + mem->pgsize * page_number;
}



////////////////////////////////////
///   Инициализация аллокатора   ///
////////////////////////////////////

// Сколько страниц нужно зарезервировать под служебные данные?
static uint64_t get_serv_pages(int levels, uint64_t pgsize, uint64_t pages){
    uint64_t serv_size = levels * sizeof(buddy_list_t) + pages;
    return serv_size / pgsize + 1;
}
 
// Заполняет всю таблицу состояний значениями BUDDY_NOTHING
static void init_state_table(buddy_allocator_t* mem){
    for(int i = 0; i < mem->pages; i++)
        mem->state_table[i] = BUDDY_NOTHING;
}

// Разпихивает все блоки по спискам свободных блоков
static void init_lists(buddy_allocator_t* mem){
    int lvl = mem->levels - 1;
    buddy_list_t* list = &mem->lists[lvl];
    list_init(list, lvl);
    char* curr_page = (char*)mem->data;
    uint64_t pages = mem->pages;    // количество оставшихся страниц

    // Изначально кусков верхнего уровня может быть много, с ними работаем отдельно
    uint64_t top_lvl_count = (pages >> lvl);
    for(int i = 0; i < top_lvl_count; i++){
        list_add(list, (buddy_free_block_t*)curr_page);
        curr_page += (1 << lvl) * mem->pgsize;
        pages -= (1 << lvl);
    }

    my_assert(pages < (1 << lvl), "");

    // Для остальных уровней не более одного куска
    while(lvl > 0){
        lvl -= 1;
        list -= 1;
        list_init(list, lvl);
        if(pages >= (1 << lvl)){
            list_add(list, (buddy_free_block_t*)curr_page);
            curr_page += (1 << lvl) * mem->pgsize;
            pages -= (1 << lvl);
        }
        my_assert(pages < (1 << lvl), "");
    }
}


int lib_buddy_init(
    buddy_allocator_t* mem, 
    int levels, uint64_t pgsize,  // гиперпараметры 
    uint64_t pages, void* ptr     // распределяемые ресурсы
){
    my_assert(levels > 0, "");
    if(sizeof(buddy_free_block_t) < pgsize)
        return -1;

    mem->levels = levels;
    mem->pgsize = pgsize;

    uint64_t serv_pages = get_serv_pages(levels, pgsize, pages);
    if(serv_pages > pages)
        return -1;

    mem->lists = (buddy_list_t*)ptr;
    mem->state_table = (char*) ptr + sizeof(buddy_list_t) * mem->levels;

    mem->pages = pages - serv_pages;
    mem->data = (char*) ptr + serv_pages * pgsize;

    init_state_table(mem);
    init_lists(mem);
    return 0;
}



////////////////////////////
///   Выделение памяти   ///
//////////////////////////// 

static int buddy_log2(uint64_t n){
    for(int log = 0; log < sizeof(n) * 8; log++){
        if(n == (1 << log))
            return log;   
    }
    return -1;
}

// Находит наименьший свободный блок уровня хотя бы lvl
static buddy_free_block_t* find_free_block(buddy_allocator_t* mem, int lvl){
    while(lvl < mem->levels){
        buddy_list_t* list = &mem->lists[lvl];
        if(list->len > 0)
            return list->head.next;
        lvl += 1;
    }
    return 0;
}

/*
Удаляет блок free_block уровня initial_lvl из списка свободных.
Отделяет от него один блок уровня final_lvl.
Возвращает указатель на отделённый блок.
Всё остальное место распадается на меньшие свободные блоки, которые добавляем в списки
*/
static void* buddy_devide(buddy_allocator_t* mem, buddy_free_block_t* free_block, int final_lvl ){
    int initial_lvl = free_block->level;
    my_assert(initial_lvl >= final_lvl, "");

    remove_block(free_block);
    while(initial_lvl > final_lvl){
        initial_lvl -= 1;

        // Вторую половину объявляем свободной, а первую продолжаем делить
        char* second_part = (char*)free_block + (mem->pgsize << initial_lvl);
        list_add( &mem->lists[initial_lvl], (buddy_free_block_t*)second_part);
    }
    return free_block;
}

void* lib_buddy_alloc(buddy_allocator_t* mem, uint64_t pages){
    /*
    0) Получаем по количеству страниц pages уровень куска (=log(pages)), который нужно выделить.
        Проверяем корректность запроса.
    1) Ищем свобоный блок наименьшего уровня, чтобы нам хвавтило места.
        Делаем с помощью прохода по спискам - ищем список с ненулевой длиной
    3) Отделяем от него блок нужного размера.
        Исходный блок удаляем из списка. Свободный остаток состоит из нескольких блоков, добавляем их в списки.
    4) Проставляем байт в state_table, что было выделение
    5) Возвращаем соответствующий адрес  
    */

    // 0
    int lvl = buddy_log2(pages);
    if(lvl == -1)
        return 0;
    my_assert(pages == 1 << lvl, "");

    // 1
    buddy_free_block_t* free_block = find_free_block(mem, lvl);
    if(free_block == 0)
        return 0;
    my_assert(free_block->level >= lvl, "");

    // 3
    void* res_block = buddy_devide(mem, free_block, lvl);

    // 4
    int pn = get_page_number(mem, res_block);
    my_assert(pn >= 0, "");
    mem->state_table[pn] = lvl;

    // 5
    return res_block;
}



///////////////////////////////
///   Освобождение памяти   ///
///////////////////////////////

static int is_neighbour_a_free_block(buddy_allocator_t* mem, int lvl, uint64_t npn){
    /*

    */
    if(npn + (1 << lvl) > mem->pages)   // что если сосед вообще не существует? (выходит за границы области аллокатора) 
        return 0;
    if(mem->state_table[npn] != BUDDY_NOTHING)
        return 0;
}

int block_exists(buddy_allocator_t* mem, int pn, int lvl){
    if(!(0 <= lvl && lvl < mem->levels))    // уровень блока корректен
        return 0;
    if(pn & ((1 << lvl) - 1) != 0)  // номер страницы кратен 2 в степени lvl
        return 0;
    if(!(0 <= pn && pn + (1 << lvl) <= mem->pages))   // блок не вылезает за границы
        return 0;
    return 1;
}

uint64_t get_neighbour_page_number(buddy_allocator_t* mem, int lvl, uint64_t pn){
    uint64_t npn = pn ^ (1LL << lvl);   // neighbour page number - номер первой страницы соседнего куска
    if(!block_exists(mem, npn, lvl))    // что если сосед вообще не существует?
        return ;
}

static void add_free_block(buddy_allocator_t* mem,  uint64_t pn, int lvl){
    /*
    До тех пор, пока сосед свободен, объединяемся с ним:
    1) Выкидываем соседа из списка
    2) Заменяем np, на min(np, npn)
    3) Повышаем уровень lvl
    В конце добавляем один большой кусок

    Как понять, что сосед свободен?
    Заметим, что его первая страница - либо первая страница выделенного блока, либо первая страница свободного блока.
    То есть, она не может лежать в середине выделенного или свободного блока. Действительно, это бы значило что и наш
    блок тоже только что был полностью занят или полностью свободен, но раз мы освободили внутри него память это не так.
    Итак:
        а) сосед начинается с выделенной страницы => точно занят
        б) сосед начинается со свободной страницы => в этой странице размещена buddy_free_block какого-то списка.
            Тут содержится уровень этого свободного блока. Если он совпадает с уровнем соседа, то сосед свободен, 
            иначе он частично занят (быть больше он не может по соображениям выше).
    */
    my_assert(lvl < mem->levels, "");
    my_assert(block_exists(mem, pn, lvl), "");
    while(lvl < mem->levels){
        // 0
        my_assert(block_exists(mem, pn, lvl), "");
        uint64_t npn = pn ^ (1LL << lvl);   // neighbour page number - номер первой страницы соседнего куска
        if(!block_exists(mem, npn, lvl))    // что если сосед вообще не существует?
            break;

        // a)
        if(mem->state_table[npn] != BUDDY_NOTHING)
            break;

        // б)
        buddy_free_block_t* free_block = get_page_ptr(mem, npn);
        my_assert(free_block->level <= lvl, "");
        if(free_block->level < lvl) // сосед частично занят
            break;

        // 1     
        remove_block(free_block);

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
        неправильный.
    1) Из таблицы состояний получаем уровень выделенного блока, или понимаем
        что по этому адресу выделения не было и паникуем.
    2) Итак, имеем корректно выделенный блок. Помечаем его свободным в таблице состояний
    3) Склеиваем его (возможно нуль или несколько раз) и добавляем в список свободных участков
    */

    // 0
    int pn = get_page_number(mem, addr);
    my_assert(pn >= 0, "");  // паникуем

    // 1
    int lvl = mem->state_table[pn];
    my_assert(lvl >= 0, "");  // паникуем
    
    // 2
    mem->state_table[pn] = BUDDY_NOTHING;

    // 3
    add_free_block(mem, lvl, pn);  
}



