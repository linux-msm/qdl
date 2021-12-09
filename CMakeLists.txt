cmake_minimum_required(VERSION 3.14)

project(qdl)

set(COMPILE_FLAGS 
        -Wall -Wextra -pedantic -Werror 
        -Wno-unused-parameter 
        -Wno-deprecated
        )

add_subdirectory(libsparse)
add_subdirectory(qdl_src)
