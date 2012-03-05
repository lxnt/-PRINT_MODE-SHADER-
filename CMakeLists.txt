set(CMAKE_BUILD_TYPE  "Debug" CACHE STRING "Debug or Release" FORCE)
cmake_minimum_required(VERSION 2.8)
project(PMS)

set(DF_VERSION "3125" CACHE STRING "DF version to build for")
add_definitions(-DDF_VERSION=0x${DF_VERSION})
if(UNIX)
    set(CMAKE_LIBRARY_PATH /usr/lib32 /usr/lib/i386-linux-gnu)
    set(CMAKE_CXX_FLAGS_DEBUG "-m32 -g -std=c++0x -Wall -Wno-sign-compare")
    set(CMAKE_C_FLAGS_DEBUG "-m32 -g -Wall")
    set(CMAKE_CXX_FLAGS "-m32 -march=i686 -mtune=generic -std=c++0x -Wno-sign-compare ")
    set(CMAKE_C_FLAGS "-m32 -march=i686 -mtune=generic")
    add_definitions( -Dlinux=23 -Dunix=42 -DLINUX_BUILD)
endif()

include_directories(dfapi/include)
link_directories(/usr/lib/i386-linux-gnu /usr/lib32)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG=8)
endif()

add_definitions( -D_GNU_SOURCE=1 -D_REENTRANT )

add_subdirectory(print_shader)
add_subdirectory(dfapi)

set(CPACK_GENERATOR TBZ2)
set(CPACK_PACKAGE_NAME "PMS")
set(CPACK_PACKAGE_VENDOR "lxnt") 
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PRINT_MODE:SHADER with fgtestbed:dump")
include(CPack)

