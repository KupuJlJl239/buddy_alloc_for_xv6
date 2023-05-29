
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

extern "C"{
    #include "buddy_alloc.h"
}

#include <cstdio>
#include <cassert>
#include <vector>

int get_page_number(buddy_allocator_t* mem, void* page_ptr){
    int d = (char*)page_ptr - (char*)mem->data;
    if(d % mem->pgsize != 0)
        return -1;
    return d / mem->pgsize;
}

uint64_t pages_in_use(buddy_allocator_t* mem){
    uint64_t res = 0;
    for(int i = 0; i < mem->pages; i++)
        if(mem->state_table[i] != BUDDY_FREE)
            res += 1;
    return res;
}

// Проверяет соответствие списков таблице состояний, а также отсутствие наложений в списках
int check(buddy_allocator_t* mem){
    char* my_state_table = (char*) malloc(mem->pages);
    for(int i = 0; i < mem->pages; i++){
        my_state_table[i] = BUDDY_USED;
    }
    for(int lvl = 0; lvl < mem->levels; lvl++){
        buddy_free_block_t* curr = mem->lists[lvl].first;
        while(curr != nullptr){
            int page_number = get_page_number(mem, (void*)curr);
            assert(page_number != -1);
            for(int i = page_number; i < page_number + (1<<lvl); i++){
                if(my_state_table[i] == BUDDY_FREE){
                    printf("free page collision: page %d\n", i);
                    free(my_state_table);
                    return -1;
                }
                my_state_table[i] = BUDDY_FREE;
            }
            curr = curr->next;
        }
    }
    for(int i = 0; i < mem->pages; i++){
        char s = mem->state_table[i];
        char ls = my_state_table[i];
        if(ls == BUDDY_FREE && s != BUDDY_FREE){
            printf("page %d is free is lists and is NOT free in table (= %d)\n", i, s);
            free(my_state_table);
            return -1;
        }
        if(ls != BUDDY_FREE && s == BUDDY_FREE){
            printf("page %d is NOT free is lists and is free in table\n", i);
            free(my_state_table);
            return -1;
        }
    }
    free(my_state_table);
    return 0;
}


TEST_CASE("init"){
    #define TEST_INIT(_levels, _pgsize, _pages, _serv_pages, ...) do{ \
        buddy_allocator_t mem; \
        void* ptr = malloc(_pgsize * _pages);  \
        lib_buddy_init(&mem, _levels, _pgsize, _pages, ptr); \
        REQUIRE_EQ(mem.levels, _levels); \
        REQUIRE_EQ(mem.pgsize, _pgsize); \
        REQUIRE_EQ(mem.pages, _pages - _serv_pages); \
        REQUIRE_EQ( \
            (char*)mem.data - (char*)ptr, \
            _serv_pages * _pgsize \
        ); \
        uint64_t free_by_size[] = __VA_ARGS__;\
        for(int i = 0; i < _levels; i++)\
            CHECK_EQ(free_by_size[i], mem.lists[i].len);\
        CHECK_EQ(check(&mem), 0);\
        free(ptr); \
    }while(0)

    TEST_INIT(1, 100, 1000, 11, {1000 - 11});
    TEST_INIT(2, 100, 1000, 11, {1, 494});          // 494 = (1000-11)/2
    TEST_INIT(3, 100, 1000, 11, {1, 0, 494 / 2});

    #undef TEST_INIT
}


TEST_CASE("bad_alloc"){
    #define TEST_BAD_ALLOC(_levels, _pgsize, _pages, ...) do{ \
        buddy_allocator_t mem; \
        void* ptr = malloc(_pgsize * _pages);  \
        lib_buddy_init(&mem, _levels, _pgsize, _pages, ptr); \
        std::vector<int> arr = __VA_ARGS__; \
        for(int i = 0; i < arr.size() - 1; i++){ \
            CHECK_NE(lib_buddy_alloc(&mem, arr[i]), nullptr); \
            REQUIRE_EQ(check(&mem), 0);\
        }\
        CHECK_EQ(lib_buddy_alloc(&mem, arr[arr.size()-1]), nullptr); \
        REQUIRE_EQ(check(&mem), 0);\
        free(ptr); \
    }while(0)

    TEST_BAD_ALLOC(10, 100, 1000, {0});
    TEST_BAD_ALLOC(10, 100, 1000, {3});
    TEST_BAD_ALLOC(10, 100, 1000, {5});
    TEST_BAD_ALLOC(10, 100, 1000, {6});
    TEST_BAD_ALLOC(10, 100, 1000, {7});
    TEST_BAD_ALLOC(10, 100, 1000, {9});
    TEST_BAD_ALLOC(10, 100, 1000, {1023});
    TEST_BAD_ALLOC(10, 100, 1000, {1024});
    TEST_BAD_ALLOC(10, 100, 1000, {1025});
    TEST_BAD_ALLOC(10, 100, 1000, {2048});

    for(int lvls = 1; lvls < 10; lvls++)
        TEST_BAD_ALLOC(lvls, 100, 1000, {1 << lvls});

    TEST_BAD_ALLOC(10, 100, 1000, {1, 2, 4, 8, 16, 32, 64, 128, 256, 512});

    #undef TEST_BAD_ALLOC
}



TEST_CASE("good alloc"){
    #define TEST_GOOD_ALLOC(_levels, _pgsize, _pages, ...) do{ \
        buddy_allocator_t mem; \
        void* ptr = malloc(_pgsize * _pages);  \
        lib_buddy_init(&mem, _levels, _pgsize, _pages, ptr); \
        std::vector<int> arr = __VA_ARGS__; \
        for(auto el: arr){ \
            CHECK_NE(lib_buddy_alloc(&mem, el), nullptr); \
            REQUIRE_EQ(check(&mem), 0);\
        }\
        free(ptr); \
    }while(0)

    for(int lvls = 1; lvls < 10; lvls++){
        for(int i = 0; i < lvls; i++){
            TEST_GOOD_ALLOC(lvls, 100, 1000, {1 << i});
        }
    }

    // Тесты на выделение всех доступных страниц
    TEST_GOOD_ALLOC(10, 100, (12 + 989), {512, 256, 128, 64, 16, 8, 4, 1});
    TEST_GOOD_ALLOC(10, 100, (12 + 989), {1, 4, 8, 16, 64, 128, 256, 512});
    TEST_GOOD_ALLOC(11, 100, (13 + 1024), {1024});

    #undef TEST_GOOD_ALLOC
}


TEST_CASE("free"){
    #define CHECK_STATE(_mem, ...) do{ \
        uint64_t free_by_size[] = __VA_ARGS__;\
        for(int i = 0; i < _levels; i++)\
            CHECK_EQ(free_by_size[i], mem.lists[i].len);\
        REQUIRE_EQ(check(&mem), 0);\
    }while(0)

    #define TEST_ALLOC_FREE(_levels, _pgsize, _pages, ...) do{ \
        buddy_allocator_t mem; \
        void* space = malloc(_pgsize * _pages);  \
        lib_buddy_init(&mem, _levels, _pgsize, _pages, space); \
        std::vector<int> arr = __VA_ARGS__; \
        std::vector<void*> ptrs;\
        for(auto el: arr){ \
            void* ptr = lib_buddy_alloc(&mem, el);\
            ptrs.push_back(ptr); \
            REQUIRE_NE(ptr, nullptr); \
            REQUIRE_EQ(check(&mem), 0);\
        }\
        for(auto ptr: ptrs){ \
            lib_buddy_free(&mem, ptr);\
            REQUIRE_EQ(check(&mem), 0);\
        }\
        CHECK_EQ(pages_in_use(&mem), 0);\
        free(space); \
    }while(0)

    // Тесты на выделение всех доступных страниц
    TEST_ALLOC_FREE(10, 100, (12 + 989), {512, 256, 128, 64, 16, 8, 4, 1});
    TEST_ALLOC_FREE(10, 100, (12 + 989), {1, 4, 8, 16, 64, 128, 256, 512});
    TEST_ALLOC_FREE(11, 100, (13 + 1024), {1024});

    #undef TEST_ALLOC_FREE
}

