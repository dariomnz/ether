cmake_minimum_required(VERSION 3.28)
project(ether LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_executable(ether 
    src/main.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/ir/ir.cpp
    src/ir/ir_gen.cpp
    src/vm/vm.cpp
)

target_include_directories(ether PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(test_runner test/test_runner.cpp)