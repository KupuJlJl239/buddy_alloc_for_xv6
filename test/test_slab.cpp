
// #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// #include "doctest.h"
#include <vector>
#include <cassert>

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



struct randmem_s{
    void* ptr;
    uint size;
};

char randseed;
char randcurr;
std::vector<randmem_s> randmem_v;

void initrand(char seed){
    randseed = seed;
    randcurr = seed;
}

char rand(){
    randcurr = 239 * randcurr + 61;
    return randcurr;
}

void randmem(void* ptr, uint size){
    randmem_v.push_back(randmem_s{.ptr = ptr, .size = size});
    for(int i = 0; i < size; i++)
        ((char*)ptr)[i] = rand();
}

void checkmem(){
    randcurr = randseed;
    for(auto& r: randmem_v){
        for(int i = 0; i < r.size; i++)
            assert(((char*)r.ptr)[i] == rand());
    }
}

int main(){
    randseed = 0;
    int levels = 10;
    uint64 pgsize = 4096;
    uint64 pages = 100;
    std::vector<char> data(pgsize * pages);
    lib_buddy_init(&mem, levels, pgsize, pages, &data[0]);

    slab_alloc_t slab;
    uint ssize = 10;
    lib_slab_init(&slab, pgsize, ssize, buddy_alloc, buddy_free, pgbegin);

    initrand(123);

    std::vector<void*> v(10000);
    for(int i = 0; i < 10000; i++){
        v[i] = lib_slab_alloc(&slab);
        randmem(v[i], ssize);
    }
    checkmem();
    for(int i = 0; i < 10000; i++){
        lib_slab_free(&slab, v[i]);
    }
}

