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
    
    dfapi_glue.cpp
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
file(GLOB SHADER_SOURCE  ${PMS_SOURCE_DIR}/print_shader/shaders/*)
add_custom_command(
    OUTPUT ${PMS_SOURCE_DIR}/print_shader/shaders.c
    COMMAND python ${PMS_SOURCE_DIR}/print_shader/grab-shaders.py ${PMS_SOURCE_DIR}/print_shader/shaders ${PMS_SOURCE_DIR}/print_shader/shaders.c
    WORKING_DIRECTORY ${PMS_SOURCE_DIR}/print_shader
    MAIN_DEPENDENCY ${PMS_SOURCE_DIR}/print_shader/grab-shaders.py
    DEPENDS ${SHADER_SOURCE}
)
add_custom_target(grab_shaders DEPENDS ${PMS_SOURCE_DIR}/print_shader/shaders.c)
add_library( graphics SHARED ${SOURCE} )
add_dependencies(graphics grab_shaders)
find_package(GTK2 2.10 REQUIRED gtk)
include_directories(${GTK2_INCLUDE_DIRS})
target_link_libraries(graphics ${GTK2_LIBRARIES} ${LIBS} dfapi)

