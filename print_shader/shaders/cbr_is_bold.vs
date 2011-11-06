#version 120
#line 2 0
#pragma optimize(off)
#pragma debug(on)

const float ANSI_CC = 16.0; // ansi color count 

uniform vec4 txsz;              // { w_tiles, h_tiles, tile_w, tile_h }
uniform vec2 viewpoint;			
uniform vec3 pszar; 			// { parx, pary, psz }
// Total 9 float uniforms, 1 sampler

attribute vec4 screen;          // { ch, fg, bg, bold } 
attribute float texpos;         //  tile_tex_idx 
attribute float addcolor;
attribute float grayscale;
attribute float cf;
attribute float cbr;
attribute vec2 position;        // almost forgot teh grid
// Total 7 attributes  = 13 float values 

varying vec4 ansicolors;        // computed foreground and background color indexes for tile and creature
varying vec2 tilecrea;         	// floor and creature tile indexes
// Total 2 texcoords  = 6 varying floats

vec2 ansiconvert(vec3 c) { // { fg, bg, bold }, returns {fg_idx, bg_idx}
    vec2 rv;
    float bold_factor = 0.0;
    if (c.z > 0.1)
        bold_factor = 8.0;

    rv.x = mod(c.x + bold_factor, ANSI_CC)/ANSI_CC;
    rv.y = mod(c.y, ANSI_CC)/ANSI_CC;
    return rv;
}

void main() {
    ansicolors.xy = ansiconvert(screen.yzw);
    ansicolors.zw = vec2(15.0, 0.0);
    tilecrea = vec2(screen.x, texpos);
    
    if (texpos > 0.1) {
        if  (grayscale > 0.1)
            ansicolors.zw = ansiconvert(vec3(cf, screen.z, cbr));
        else if (addcolor > 0.1)
            ansicolors.zw = ansicolors.xy;
    } else {
        tilecrea.y = -42; // no creature.
    }

    vec2 posn = pszar.xy*position*pszar.z - viewpoint;
     
    gl_Position = gl_ModelViewProjectionMatrix*vec4(posn.x, posn.y, 0.0, 1.0);
    gl_PointSize = pszar.z;    
}
