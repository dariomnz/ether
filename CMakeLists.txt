cmake_minimum_required(VERSION 3.28)
project(ether LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

file(GLOB_RECURSE SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_executable(ether ${SOURCES})

target_include_directories(ether PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_library(LIBURING uring)
if(NOT LIBURING)
    message(FATAL_ERROR "liburing not found")
endif()
target_link_libraries(ether PRIVATE ${LIBURING})