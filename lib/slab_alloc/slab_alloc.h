

#ifdef XV6
    #include "kernel/types.h"
    typedef uint64 uint64_t;
#else
    #include <stdint.h>
#endif


typedef struct{
    uint64_t pgsize;
    uint64_t ssize;
    void* (*alloc)(void);
    void (*free)(void*);
    // TODO!   
} slab_alloc_t;


// TODO!