cmake_minimum_required(VERSION 3.14)
project(qdl C)

set(CMAKE_C_STANDARD 11)


find_package(LibXml2 REQUIRED libxml-2.0)
find_package(LibXml2 REQUIRED)
find_path(LIBUSB_INCLUDE_DIR
        NAMES libusb.h
        PATH_SUFFIXES "include" "libusb" "libusb-1.0")
message("LIBUSB_INCLUDE_DIR: " ${LIBUSB_INCLUDE_DIR})
find_library(LIBUSB_LIBRARY
        NAMES usb-1.0
        PATH_SUFFIXES "lib" "lib32" "lib64")
message("LIBUSB_LIBRARY: " ${LIBUSB_LIBRARY})

include_directories(.)

add_executable(qdl
        firehose.c
        patch.c
        patch.h
        program.c
        program.h
        qdl.c
        qdl.h
        sahara.c
        ufs.c
        ufs.h
        util.c)
target_include_directories(qdl PUBLIC ${LIBXML2_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
target_link_libraries(qdl ${LIBXML2_LIBRARIES} ${LIBUSB_LIBRARY})