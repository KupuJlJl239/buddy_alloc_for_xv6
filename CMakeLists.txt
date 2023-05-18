

project(test_buddy CXX)

add_subdirectory(lib/buddy_alloc)
add_subdirectory(lib/slab_alloc)

add_executable(test_buddy test/test_buddy.cpp)
target_link_libraries(test_buddy buddy_alloc)

add_executable(test_slab test/test_slab.cpp)
target_link_libraries(test_slab slab_alloc buddy_alloc)