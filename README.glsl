Welcome to the [PRINT_MODE:SHADER]/renderer_glsl project.

This is an implementation of Dwarf Fortress 31.25 renderer 
using OpenGL 2.0 and GLSL 1.2. 

Resources:
    Forum thread: http://www.bay12forums.com/smf/index.php?topic=94528.0
    Source:       https://github.com/lxnt/-PRINT_MODE-SHADER-
    Download:     http://dffd.wimbli.com/file.php?id=5044

Current state:

Game is playable. Zoom and resize do not work to my liking, but can
be lived with. Graphics and tileset tile sizes are fixed to 16x16 pixels.
Download link above points to a compressed libprint_shader.so (with full debug,
that's why the size), compiled for linux-x86. 
Replace your libs/libgraphics.so with it to try this out.

System requirements:

I am developing this on an Ubuntu Maverick 32bit machine and an Radeon 4850
with open-source drivers from xorg-edgers repo.

OpenGL renderer string: Mesa DRI R600 (RV770 9440) 
20090101 x86/MMX+/3DNow!+/SSE2 TCL DRI2

Graphics hardware and driver requirements are in fact minimal, 
the code should work on anything that more-or-less supports GLSL 1.2

Features not found in other renderers:

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
[DUMP_SCREEN:0] - control screen and texture dumping.
    0 - no dumping
    N - dumps texture on upload to the GPU, 
        dumps complete screen data every Nth frame

Project needs:

 - Someone who can compile this for windows, preferably using Eclipse CDT.

 - More testing.

What next?

 - fix the zoom/resize functionality, test with movies too.
 
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
 

   
   
   
 
