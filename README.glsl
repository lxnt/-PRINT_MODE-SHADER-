Welcome to the [PRINT_MODE:SHADER]/renderer_glsl project.
=========================================================

This is an implementation of Dwarf Fortress 31.25 renderer 
using OpenGL 2.0 and GLSL 1.2. 

Resources:
    Forum thread: http://www.bay12forums.com/smf/index.php?topic=94528.0
    Source:       https://github.com/lxnt/-PRINT_MODE-SHADER-
    Download:     http://dffd.wimbli.com/file.php?id=5044


Current state:
==============

Game is playable. Graphics and tileset tile sizes are fixed to the size of first
tile loaded, this always is the 0th character of the font (tileset).

Creature rendering is currently broken.

Download link above points to a compressed libprint_shader.so (with full debug,
that's why the size). Replace your libs/libgraphics.so with it to try this out.

System requirements:
====================

I was developing this on an Ubuntu Maverick 32bit machine and an Radeon 4850
with open-source drivers from xorg-edgers repo.

OpenGL renderer string: Mesa DRI R600 (RV770 9440) 
20090101 x86/MMX+/3DNow!+/SSE2 TCL DRI2

Now I develop it on Ubuntu Natty 64bit with stock drivers, same card.

The code should work on anything that more-or-less supports GLSL 1.2
Specific graphics hardware and driver requirements:

GL_MAX_VERTEX_ATTRIBS=16, needed=7
GL_MAX_VERTEX_UNIFORM_COMPONENTS=4096, needed=7
GL_MAX_FRAGMENT_UNIFORM_COMPONENTS=4096, needed=6
GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS=16, needed=1
GL_MAX_TEXTURE_IMAGE_UNITS=16, needed=1
GL_MAX_VARYING_FLOATS=64, needed=20
GL_MAX_TEXTURE_COORDS=8, needed=5
GL_POINT_SIZE_MIN=0, needed=4
GL_POINT_SIZE_MAX=8192, needed=63

This is output on each run, so you will see if your box is not up 
to the task. You can also check out which cards are capable of what
at http://www.kludx.com/


Features not found in other renderers:
======================================

 - minimal processing on the CPU, data is uploaded unchanged
   to the GPU. Grid is uploaded as one vertex per tile 
   as opposed to 6 vertices per tile of renderer_opengl family.
   
 - font and graphics are assembled in-memory into one large texture
   without any sorting, assuming uniform tile size and uploaded once. 
   This is noticeably faster than the one GL call per tile approach 
   used by renderer_opegl family and also skips texture coordinates lookup
   and upload entirely.
   
 - graphics (creatures) are rendered on top of (instead of instead of)
   floor tiles in most cases, leaving parts of floor visible behind them.
   This requires one pass over the screen and screentexpos arrays 
   and maintenance of 'shadow' screen_underlay array. 
   grid_x*grid_y int32 compares and some amount of int32 writes per frame.

init.txt tokens specific to this renderer:

[PRINT_MODE:SHADER] - enables the renderer 
[VERTEX_SHADER:data/vertex.shader] - override embedded shader code
[FRAGMENT_SHADER:data/fragment.shader] - same for fragment shader
[SHADER_SET:standard] embedded shader set name. Name list is output at startup.
[DUMP_STUFF:0] - control screen and texture dumping.
    0 - no dumping
    N - dumps texture on upload to the GPU, 
        dumps complete screen data every Nth frame
[DUMP_TO:blahblah] - sets prefix for dumps. Default: "dfdump"
[SNAP_WINDOW:NO]  snap window size to not leave black margins (resize only)
[USE_UNDERLAY:NO]  attempt to remember the floor tile under creatures
[DUMP_CREATURES:NO] dump creature draw data to stderr

Project needs:
==============
 
 - More testing.


What next?
==========

 - remove 16x16 limit for graphics/tile sets. I intend to 
   fully decouple fonts and graphics, the only requirement
   left would be that all graphics are of same size, not 
   necessarily the same as tileset (font) size. Aspect ratio
   can also be different between graphicset and tileset, this
   would require user to decide which one would be used for 
   rendering.
   
 - some sane logging system. fprintfs and cerr<<s are ugly.
   
 - decouple interface rendering from world view rendering. 
   I intend to draw interface in a second pass over world view,
   with different zoom restrictions so as to keep it readable
   while you look over all your map. That will also allow 
   separate fonts for interface and tileset.
   
 - look into making scrolling smooth and using mouse as input for it. 
   
 - implement interface rendering using Pango. This will bring 
   true-type fonts in.
   
 - implement a proper interprocess communication API in df_hack. 
   This would be posix shared memory/posix message queues on
   Linux and something similar on Windows.
   
 - rewrite Stonesense in SDL+GLSL and the aforementioned dfhack API,
   so that it would run at an acceptable speed.

 - ???
 

Cross compilation on x86_64 architecture. 
=========================================

First of all, please keep in mind, that current multilib support, 
at least in Ubuntu/Debian is not intended for compiling and, even
less, linking for 32bit i686 target. Thus the process is not quite 
as straightforward as we certainly would have liked.

How to do it on Ubuntu Natty 64bit:

0. Get Eclipse Indigo with CDT (eclipse.org). 
Import the project from the github. 
Use the XC-Debug build target.

1. You will need development packages (-dev) for gtk2.0, libSDL 1.2, 
libSDL_ttf-2.0, libSDL_image-1.2, libsndfile, OpenAL, Mesa.
You will also need ia32-libs package.

2. libGLEW1.5 is not included in ia32-libs package, so you will need to fetch
i386 package from a mirror:

wget http://mirror.yandex.ru/ubuntu/pool/main/g/glew1.5/libglew1.5_1.5.7.is.1.5.2-1ubuntu4_i386.deb

Do not install it. Instead, decompress with dpkg-deb:

dpkg-deb -x libglew1.5_1.5.7.is.1.5.2-1ubuntu4_i386.deb glew32

and put library files into /usr/lib32:

ln -s  libGLEW.so.1.5 glew32/usr/lib/libGLEW.so
sudo chown 0:0 glew32/usr/lib/libGLEW.so*
sudo mv glew32/usr/lib/libGLEW.so* /usr/lib32

3. glibconfig.h This file is architecture-specific, so do not use the one in 
/usr/lib/x86_64-linux-gnu/glib-2.0/include/ - the x86_64 is in the path for a reason.
Instead fetch -dev package for i386:

wget http://mirror.yandex.ru/ubuntu/pool/main/g/glib2.0/libglib2.0-dev_2.28.6-0ubuntu1_i386.deb

decompress:

dpkg-deb -x libglib2.0-dev_2.28.6-0ubuntu1_i386.deb glib32

and put the glibconfig.h from glib32 somewhere. Then point Eclipse 
(project->properties->C/C++Build->Settings->GCC C++ Compiler->Includes)
there.

4. final step:
sudo ln -s /usr/lib32/libgcc_s.so.1 /usr/lib/x86_64-linux-gnu/gcc/x86_64-linux-gnu/4.5/32/libgcc_s.so


Now you are more or less ready to hit Ctrl-B.


Running it on x86_64 system.
============================

1. If you use open source graphics drivers, put  

export LIBGL_DRIVERS_PATH=/usr/lib32/dri-alternates

into the ./df script. This selects classic Mesa (non-Gallium)
drivers. Gallium ones that ship with Ubuntu ia32-libs are outdated
and do not contain needed bugfixes.

2. You can delete/rename  libstdc++.so.6 and libgcc_s.so.1
that are in df_linux/libs directory - system ones work fine,
and might have some improvements made after the shipped ones
were compiled.








