project(PMS)

cmake_minimum_required(VERSION 2.8)

if(UNIX)
    set(CMAKE_LIBRARY_PATH /usr/lib32 /usr/lib/i386-linux-gnu)
    set(CMAKE_CXX_FLAGS_DEBUG "-m32 -g -std=c++0x -Wall -Wno-sign-compare")
    set(CMAKE_C_FLAGS_DEBUG "-m32 -g -Wall")
    set(CMAKE_CXX_FLAGS "-m32 -march=i686 -mtune=generic -std=c++0x -Wno-sign-compare ")
    set(CMAKE_C_FLAGS "-m32 -march=i686 -mtune=generic")
endif()

link_directories(/usr/lib/i386-linux-gnu /usr/lib32)

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING 
        "Choose the type of build, options are: Debug Release."
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG=8 -Dlinux=23 -Dunix=42)
endif()

add_definitions( -D_GNU_SOURCE=1 -D_REENTRANT )

option(WITH_FMOD "include fmod" OFF)
if (WITH_FMOD)
else()
    add_definitions(-DNO_FMOD=1)
endif()

set ( LIBS 
    SDL-1.2
    sndfile
    SDL_image-1.2
    SDL_ttf-2.0
    GLU
    GLEW
    m
    png12
    rt
    openal
)

set( SOURCE
    basics.cpp
    command_line.cpp
    enabler.cpp
    enabler_input.cpp
    files.cpp
    find_files_posix.cpp
    graphics.cpp
    init.cpp
    interface.cpp
    keybindings.cpp
    KeybindingScreen.cpp
    music_and_sound_fmodex.cpp
    music_and_sound_openal.cpp
    random.cpp
    resize++.cpp
    textlines.cpp
    textures.cpp
    ttf_manager.cpp
    ViewBase.cpp
    win32_compat.cpp
)

set( HEADERS
    basics.h
    bimap.h
    command_line.h
    curses.h
    enabler.h
    enabler_input.h
    endian.h
    files.h
    find_files.h
    g_basics.h
    graphics.h
    IMG_savepng.h
    init.h
    interface.h
    KeybindingScreen.h
    keybindings.h
    mail.hpp
    music_and_sound_fmodex.h
    music_and_sound_g.h
    music_and_sound_openal.h
    music_and_sound_v.h
    platform.h
    random.h
    renderer_2d.hpp
    renderer_glsl.hpp
    renderer_opengl.hpp
    resize++.h
    svector.h
    template.h
    textlines.h
    texture_handler.h
    ttf_manager.hpp
    ViewBase.h
)

set_source_files_properties(shaders.c PROPERTIES GENERATED TRUE)
file(GLOB SHADER_SOURCE  ${PMS_SOURCE_DIR}/shaders/*)
add_custom_command(
    OUTPUT ${PMS_SOURCE_DIR}/shaders.c
    COMMAND python ${PMS_SOURCE_DIR}/grab-shaders.py ${PMS_SOURCE_DIR}/shaders ${PMS_SOURCE_DIR}/shaders.c
    WORKING_DIRECTORY ${PMS_SOURCE_DIR}
    MAIN_DEPENDENCY ${PMS_SOURCE_DIR}/grab-shaders.py
    DEPENDS ${SHADER_SOURCE}
)
add_custom_target(grab_shaders DEPENDS ${PMS_SOURCE_DIR}/shaders.c)
add_library( graphics SHARED ${SOURCE} )
add_dependencies(graphics grab_shaders)
find_package(GTK2 2.10 REQUIRED gtk)
include_directories(${GTK2_INCLUDE_DIRS})
target_link_libraries(graphics ${GTK2_LIBRARIES} ${LIBS})
