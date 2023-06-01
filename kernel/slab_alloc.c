#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "lib/slab_alloc/slab_alloc.h"

// #include "slab_alloc.h"
#include "virtio.h"
#include "pipe.h"

typedef struct{
    struct spinlock lock;
    slab_alloc_t slab;
} kslab_alloc_t;


static void* pgbegin(void* ptr){
    uint64 x = (uint64) ptr;
    return ptr - x % PGSIZE;
}


static void kslab_init(
    kslab_alloc_t* slab,
    uint ssize
){
    initlock(&slab->lock, "slab lock");
    lib_slab_init(&slab->slab, PGSIZE, ssize, buddy_alloc, buddy_free, pgbegin);
}

static void* kslab_alloc(kslab_alloc_t* slab){
    acquire(&slab->lock);
    void* res = lib_slab_alloc(&slab->slab);
    release(&slab->lock);
    return res;
}

static void kslab_free(kslab_alloc_t* slab, void* ptr){
    acquire(&slab->lock);
    lib_slab_free(&slab->slab, ptr);
    release(&slab->lock);
}




kslab_alloc_t slab_virtq_desc;
kslab_alloc_t slab_virtq_avail;
kslab_alloc_t slab_virtq_used;
kslab_alloc_t slab_pipe;


void slab_init(){
    kslab_init(&slab_virtq_desc, sizeof(struct virtq_desc));
    kslab_init(&slab_virtq_avail, sizeof(struct virtq_avail));
    kslab_init(&slab_virtq_used, sizeof(struct virtq_used));
    kslab_init(&slab_pipe, sizeof(struct pipe));
}



void* slab_alloc(int slab_struct){
    void* res = 0;
    if(slab_struct == SLAB_virtq_desc)
        res = kslab_alloc(&slab_virtq_desc);
    else if(slab_struct == SLAB_virtq_avail)
        res = kslab_alloc(&slab_virtq_avail);
    else if(slab_struct == SLAB_virtq_used)
        res = kslab_alloc(&slab_virtq_used);
    else if(slab_struct == SLAB_pipe)
        res = kslab_alloc(&slab_pipe);
    else 
        panic("slab alloc");
    return res;
}


void slab_free(int slab_struct, void* ptr){
    if(slab_struct == SLAB_virtq_desc)
        kslab_free(&slab_virtq_desc, ptr);
    else if(slab_struct == SLAB_virtq_avail)
        kslab_free(&slab_virtq_avail, ptr);
    else if(slab_struct == SLAB_virtq_used)
        kslab_free(&slab_virtq_used, ptr);
    else if(slab_struct == SLAB_pipe)
        kslab_free(&slab_pipe, ptr);
    else 
        panic("slab free");
}





