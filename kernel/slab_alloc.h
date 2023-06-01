#include "virtio.h"

enum SlabStructs{
    SLAB_virtq_desc, 
    SLAB_virtq_used_elem,
    SLAB_virtio_blk_req
};


void slab_init();
void* slab_alloc(int slab_struct);
void slab_free(int slab_struct, void* ptr);


