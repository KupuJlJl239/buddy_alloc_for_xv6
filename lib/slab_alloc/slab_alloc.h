#pragma once

#ifdef XV6
    #include "kernel/types.h"   
#else
    typedef unsigned int uint;
    typedef unsigned long uint64;
#endif



struct slab_list;


typedef struct{
    struct slab_list* lists;
    uint pgsize;
    uint ssize;     // struct size
    uint cells;     // cells in page
    void* (*buddy_alloc)(uint64);
    void (*buddy_free)(void*);
    void* (*pgbegin)(void*); 
} slab_alloc_t;


void lib_slab_init(
    slab_alloc_t* slab,
    uint pgsize,
    uint ssize,
    void* (*buddy_alloc)(uint64),
    void (*buddy_free)(void*),
    void* (*pgbegin)(void*)
);


void* lib_slab_alloc(slab_alloc_t* slab);
void lib_slab_free(slab_alloc_t* slab, void* ptr);