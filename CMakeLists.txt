cmake_minimum_required(VERSION 3.10.2)

project(qdl VERSION "1.0.0" LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_C_EXTENSIONS TRUE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XML libxml-2.0 REQUIRED)
pkg_check_modules(UDEV libudev REQUIRED)

add_definitions(${XML_CFLAGS} ${XML_CFLAGS_OTHER})

add_executable(qdl firehose.c qdl.c sahara.c util.c patch.c program.c ufs.c)
target_link_libraries(qdl PUBLIC ${XML_LINK_LIBRARIES} ${UDEV_LINK_LIBRARIES})

add_executable(ks ks.c sahara.c util.c)
target_link_libraries(ks PUBLIC ${XML_LINK_LIBRARIES})

install(TARGETS qdl ks DESTINATION bin)
