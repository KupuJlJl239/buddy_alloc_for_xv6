project(slab_alloc C)

add_library(slab_alloc slab_alloc.c)
target_include_directories(slab_alloc PUBLIC .)