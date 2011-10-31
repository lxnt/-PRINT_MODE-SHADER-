#version 120
#line 2 0
#pragma optimize(off)
#pragma debug(on)

const float ANSI_CC = 16.0; // ansi color count 

uniform sampler2D txco;
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
varying vec4 tile;         	// tile: offset into the font texture and tile size
varying vec4 creature;          // creature: offset into the font texture and tile size
// Total 3 texcoords  = 12 varying floats

vec2 ansiconvert(vec3 c) { // { fg, bg, bold }, returns {fg_idx, bg_idx}
    vec2 rv;
    float bold_factor = 0.0;
    if (c.z > 0.1)
        bold_factor = 8.0;

    rv.x = mod(c.x + bold_factor, ANSI_CC)/ANSI_CC;
    rv.y = mod(c.y, ANSI_CC)/ANSI_CC;
    return rv;
}

vec4 idx2texco(float idx) {
    vec4 tile_size;
    vec4 rv;
    
    rv.x = fract( idx / txsz.x );  	    // normalized coords 
    rv.y = floor( idx / txsz.x ) / txsz.y;  // into font texture - "offset"

    tile_size = texture2D(txco, rv.xy);
    rv.zw = tile_size.xy*256/txsz.zw ; // pixel size of the tile normalized to maxtilesize
    
    return rv;
}

void main() { // precomputes whatever there can be precomputed
    
    ansicolors.xy = ansiconvert(screen.yzw);
    tile = idx2texco(screen.x);
    
    vec2 defaultcolors = vec2(0.0, 15.0);
    if (texpos > 0.1) {
	creature = idx2texco(texpos);
	if  (grayscale > 0.1) {  
	    ansicolors.zw = ansiconvert(vec3(cf, screen.z, cbr));
	} else if (addcolor > 0.1) {
	    ansicolors.zw = ansicolors.xy;
	} else {
	    ansicolors.zw = defaultcolors;
	}
    } else {
	creature = vec4(0.0); // size of 0 = no creature.
	ansicolors.zw = defaultcolors;
    }

    vec2 posn = pszar.xy*position*pszar.z - viewpoint;
     
    gl_Position = gl_ModelViewProjectionMatrix*vec4(posn.x, posn.y, 0.0, 1.0);
    gl_PointSize = pszar.z;    
}
