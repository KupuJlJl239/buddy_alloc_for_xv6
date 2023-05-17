
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

extern "C"{
    #include "buddy_alloc.h"
}

#include <cstdio>


TEST_CASE("init"){
    #define TEST_INIT(_levels, _pgsize, _pages, _serv_pages, ...) do{ \
        printf(\
            "Test init(levels = %d, pgsize = %d, pages = %d) \
            serv_data =? %d pages", \
            _levels, _pgsize, _pages, _serv_pages \
        );\
        buddy_allocator_t mem; \
        void* ptr = malloc(_pgsize * _pages);  \
        buddy_init(&mem, _levels, _pgsize, _pages, ptr); \
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
        free(ptr); \
    }while(0)

    TEST_INIT(1, 100, 1000, 11, {1000 - 11});
    TEST_INIT(2, 100, 1000, 11, {1, 494});  // 494 = (1000-11)/2
    TEST_INIT(3, 100, 1000, 11, {1, 0, 494 / 2});

    #undef TEST_INIT
}