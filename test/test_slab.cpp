
// #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// #include "doctest.h"
#include <vector>

extern "C"{
    #include "buddy_alloc.h"
    #include "slab_alloc.h"
}



buddy_allocator_t mem;

void* buddy_alloc(uint64 pages){
    return lib_buddy_alloc(&mem, pages);
}

void buddy_free(void* ptr){
    lib_buddy_free(&mem, ptr);
}

void* pgbegin(void* ptr){
    uint d = (char*)ptr - (char*)mem.data;
    return (char*)ptr - d % mem.pgsize;
}



int main(){
    int levels = 10;
    uint64 pgsize = 4096;
    uint64 pages = 100;
    std::vector<char> data(pgsize * pages);
    lib_buddy_init(&mem, levels, pgsize, pages, &data[0]);

    slab_alloc_t slab;
    lib_slab_init(&slab, pgsize, 10, buddy_alloc, buddy_free, pgbegin);

    std::vector<void*> v(10000);
    for(int i = 0; i < 10000; i++){
        v[i] = lib_slab_alloc(&slab);
    }
    for(int i = 0; i < 10000; i++){
        lib_slab_free(&slab, v[i]);
    }
}

