cmake_minimum_required(VERSION 3.14)
project(libsparse C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_COMP)

find_package(ZLIB)
message("ZLIB_INCLUDE_DIR: " ${ZLIB_INCLUDE_DIR})
message("ZLIB_LIBRARY: " ${ZLIB_LIBRARY})

add_library(libsparse STATIC
        backed_block.cpp
        output_file.cpp 
        sparse.cpp 
        sparse_crc32.cpp 
        sparse_err.cpp 
        sparse_read.cpp 
        stringprintf.cpp
        )

include_directories(include)


target_include_directories(libsparse PUBLIC include ${ZLIB_INCLUDE_DIR})
target_link_libraries(libsparse ${ZLIB_LIBRARY})
target_compile_options(libsparse PRIVATE -fPIC)
