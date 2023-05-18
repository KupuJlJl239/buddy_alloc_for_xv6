// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "lib/buddy_alloc/buddy_alloc.h"


extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct {
  struct spinlock lock;
  buddy_allocator_t mem;
} buddy_mem;


void 
buddy_init()
{
    initlock(&buddy_mem.lock, "buddy_mem"); 

    void* first_page = (void*)PGROUNDUP((uint64)end);
    uint64 space_size = (char*)PHYSTOP - (char*)first_page;
    if(space_size % PGSIZE != 0)
        panic("");

    lib_buddy_init(
        &buddy_mem.mem, 
        10, PGSIZE, 
        space_size/PGSIZE, 
        (void*)first_page
    );
}

void* 
buddy_alloc(uint64 pages)
{
    acquire(&buddy_mem.lock);
    void* ptr = lib_buddy_alloc(&buddy_mem.mem, pages);
    release(&buddy_mem.lock);
    return ptr;
}

void 
buddy_free(void* addr)
{
    acquire(&buddy_mem.lock);
    lib_buddy_free(&buddy_mem.mem, addr);
    release(&buddy_mem.lock);
}
