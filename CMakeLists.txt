

project(test_buddy C CXX)
add_executable(test_buddy test/test.cpp test/buddy_alloc.c)

#cmake_minimum_required(VERSION 3.16)



#add_library(buddy_allocation test/buddy_alloc.c)
#target_include_directories(buddy_allocation PUBLIC test)

#add_executable( ${PROJECT_NAME} test/test.cpp )
#target_link_libraries( ${PROJECT_NAME} buddy_allocation )
#target_include_directories(${PROJECT_NAME} PUBLIC test)