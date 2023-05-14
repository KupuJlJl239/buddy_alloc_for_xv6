
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "buddy_alloc.h"
#include <cstdio>

int round_up(int x, int n){
    int r = x % n;
    return x - r + n;
}

TEST_CASE("init"){
    #define TEST_INIT(_levels, _pgsize, _pages) do{ \
        int serv_data = sizeof(buddy_list_t) * _levels + _pages;\
        int serv_pages = round_up(serv_data, _pgsize) / _pgsize;\
        printf(\
            "Test init(levels = %d, pgsize = %d, pages = %d) \
            serv_data = %d bytes, %d pages", \
            _levels, _pgsize, _pages, serv_data, serv_pages \
        );\
        buddy_allocator_t mem; \
        void* ptr = malloc(_pgsize * _pages);  \
        buddy_init(&mem, _levels, _pgsize, _pages, ptr); \
        REQUIRE_EQ(mem.levels, _levels); \
        REQUIRE_EQ(mem.pgsize, _pgsize); \
        REQUIRE_EQ(mem.pages, _pages - serv_pages); \
        REQUIRE_EQ( \
            (char*)mem.data - (char*)ptr, \
            serv_pages * _pgsize \
        ); \
        free(ptr); \
    }while(0)

    TEST_INIT(1, 110, 1000);
    TEST_INIT(10, 110, 1000);
    CHECK_EQ(test(), 1);


    #undef TEST_INIT

}

// int main(){
//     test();
// }