project(buddy_alloc C)

add_library(buddy_alloc buddy_alloc.c)
target_include_directories(buddy_alloc PUBLIC .)