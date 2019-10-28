cmake_minimum_required(VERSION 3.14)
project(libsparse CXX)

set(CMAKE_C_STANDARD 11)

add_library(libsparse STATIC
        backed_block.cpp
        output_file.cpp 
        sparse.cpp 
        sparse_crc32.cpp 
        sparse_err.cpp 
        sparse_read.cpp 
        stringprintf.cpp
        )

target_include_directories(libsparse PUBLIC include)
target_compile_options(libsparse PRIVATE -Werror -Wall)
