#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

extern "C"{
    #include "buddy_alloc.h"
}

#include <cstdio>
#include <cassert>
#include <vector>
#include <list>

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


enum BuddyState{
    BUDDY_FREE,
    BUDDY_USED,
    BUDDY_UNKNOWN,
};

void check(buddy_allocator_t* mem){
    std::vector<char> page_state(mem->pages);
    for(char& el: page_state)
        el = BUDDY_UNKNOWN;
    for(int i = 0; i < mem->pages; i++){
        int lvl = mem->state_table[i];
        assert(lvl < mem->levels);
        if(lvl >= 0){
            for(int j = 0; j < (1 << lvl); j++){
                assert(page_state[i + j] == BUDDY_UNKNOWN);
                page_state[i + j] = BUDDY_USED;
            }
        }
    }
    for(int lvl = 0; lvl < mem->levels; lvl++){
        buddy_list_t* list = &mem->lists[lvl];
        assert(list->head.level == lvl);
        assert(list->head.prev == 0);
        assert(list->head.list == list);
        
        buddy_free_block_t* curr = list->head.next;
        std::size_t len = 0;
        while(curr != 0){
            assert(curr->level == lvl);
            int pn = get_page_number(mem, (void*)curr);
            assert(pn != -1);
            assert(pn % (1 << lvl) == 0);
            for(int j = 0; j < (1 << lvl); j++){            
                assert(page_state[pn + j] == BUDDY_UNKNOWN);
                page_state[pn + j] = BUDDY_USED;
            }
            curr = curr->next;
            len += 1;
        }
        assert(len == list->len);
    }
    for(int i = 0; i < mem->pages; i++){
        assert(page_state[i] != BUDDY_UNKNOWN);
    }
}

struct alloc_block{
    std::size_t size;
    void* ptr;
    alloc_block(std::size_t size, void* ptr):
        size(size), ptr(ptr)
    {}
};


struct BuddyAllocator{
    buddy_allocator_t mem;
    std::vector<char> data;
    std::list<alloc_block> alloc_blocks;
    BuddyAllocator(    
        int levels,         // количество уровней в аллокаторе
        uint64_t pgsize,    // размер страницы
        uint64_t pages      // число страниц
    ){
        data.resize(pgsize * pages);
        if(lib_buddy_init(&mem, levels, pgsize, pages, &data[0]) != 0)
            throw "buddy init failed";
        check(&mem);
    }

    void* alloc(int pages){
        void* res = lib_buddy_alloc(&mem, pages);
        check(&mem);
        alloc_blocks.push_back(alloc_block(pages, res));
        return res;
    }

    void free(void* addr){
        lib_buddy_free(&mem, addr);
        alloc_blocks.remove_if([&](alloc_block& b){return b.ptr == addr;});
        check(&mem);
    }
};

TEST_CASE("init"){
    BuddyAllocator(1, 100, 100);
    BuddyAllocator(1, sizeof(buddy_free_block_t), 100);
    CHECK_THROWS( BuddyAllocator(1, sizeof(buddy_free_block_t) - 1, 100) );
    CHECK_THROWS( BuddyAllocator(1, 1000, 0) );

    BuddyAllocator(10, 1000, 1000);
}


TEST_CASE("init_"){
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
        check(&mem);\
        free(ptr); \
    }while(0)

    TEST_INIT(1, 100, 1000, 11, {1000 - 11});
    TEST_INIT(2, 100, 1000, 11, {1, 494});          // 494 = (1000-11)/2
    TEST_INIT(3, 100, 1001, 12, {1, 0, 494 / 2});

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
            check(&mem);\
        }\
        CHECK_EQ(lib_buddy_alloc(&mem, arr[arr.size()-1]), nullptr); \
        check(&mem);\
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
            check(&mem);\
        }\
        free(ptr); \
    }while(0)

    for(int lvls = 1; lvls < 10; lvls++){
        for(int i = 0; i < lvls; i++){
            TEST_GOOD_ALLOC(lvls, 100, 1000, {1 << i});
        }
    }

    // Тесты на выделение всех доступных страниц
    TEST_GOOD_ALLOC(10, 10000, (1 + 1024 - 1), {512, 256, 128, 64, 32, 16, 8, 4, 2, 1});
    TEST_GOOD_ALLOC(10, 10000, (1 + 1024 - 1), {1, 2, 4, 8, 16, 32, 64, 128, 256, 512});
    TEST_GOOD_ALLOC(11, 10000, (1 + 1024), {1024});

    #undef TEST_GOOD_ALLOC
}


TEST_CASE(""){
    BuddyAllocator mem(10, 4096, 1001);
    
    REQUIRE_EQ(mem.mem.pages, 1000);
    for(int i = 0; i < 1000; i++){
        mem.alloc(1);
    }

    for(int i = 0; i < 1000; i++){
        mem.free(mem.alloc_blocks.begin()->ptr);
    }

}