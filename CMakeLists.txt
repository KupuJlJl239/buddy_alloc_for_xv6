

project(test_buddy CXX)

add_subdirectory(lib/buddy_alloc)

add_executable(test_buddy test/test.cpp)
target_link_libraries(test_buddy buddy_alloc)