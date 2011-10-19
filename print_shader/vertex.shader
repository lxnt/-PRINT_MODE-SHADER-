#version 120
#line 1 0

/*
    row-major order.
    idx, rows, columns

    column = fract(idx/columns)*columns (aka mod(idx, rows))
    row = floor(idx/columns)

    nc_t = column/columns = fract(idx/columns)
    nc_s = row/rows = floor(idx/columns)/rows
*/

const float ANSI_CC = 16.0; // ansi color count 

uniform sampler1D ansi;
uniform vec2 txsz;              // { w_tiles, h_tiles }
uniform vec2 viewpoint;			
uniform vec3 pszar; 			// { parx, pary, psz }
// Total 7 float values, 1 sampler

attribute vec4 screen;          // { ch, fg, bg, bold } 
attribute float texpos;         //  tile_tex_idx 
attribute float addcolor;
attribute float grayscale;
attribute float cf;
attribute float cbr;
attribute vec2 position;        // almost forgot teh grid
// Total 7 attributes  = 13 float values 

varying vec4 fg_color;          // tile: computed foreground color
varying vec4 bg_color;          // tile: computed background color
varying vec4 cf_color;          // creature: computed foreground color
varying vec4 cb_color;          // creature: computed foreground color
varying vec4 texoffset;         // tile and creature's offset into font texture
// Total 5 texcoords  = 20 varying floats

vec2 ansiconvert(vec3 c) { // { fg, bg, bold }
    
    float bold_factor = 0;
    if (c.z > 0.1)
        bold_factor = 8;

    float fg_tc = mod(c.x + bold_factor, ANSI_CC) / ANSI_CC;
    float bg_tc = mod(c.y, ANSI_CC) / ANSI_CC;
    
    return vec2(fg_tc, bg_tc);
}

void main() { // precomputes whatever there can be precomputed           
    fg_color = vec4(1, 1, 1, 1);
    bg_color = vec4(0, 0, 0, 1);

    float scr_idx;
    float cre_idx;

    vec2 ansi_c = ansiconvert(screen.yzw);
    
    if (texpos > 0.1) {
        cre_idx = texpos;
        if  (grayscale > 0.1) {  
            cf_color = texture1D(ansi, cf/ANSI_CC);
            cb_color = texture1D(ansi, cbr/ANSI_CC);
        } else if (addcolor > 0.1) {
            cf_color = texture1D(ansi, ansi_c.x);
            cb_color = texture1D(ansi, ansi_c.y);
        } else {	    
            cf_color = vec4(1,1,1,1);
            cb_color = vec4(0,0,0,1);
    	}
        texoffset.z = fract( cre_idx / txsz.x );          // this magically does not depend
        texoffset.w = floor( cre_idx / txsz.x ) / txsz.y; // on graphics' aspect ratio or size.             
    } else {
        cf_color = vec4(1,1,1,1);
        cb_color = vec4(0,0,0,1);
        texoffset.z = -8.23;
        texoffset.w = -23.42;
    }
    
    scr_idx = screen.x;
    fg_color = texture1D(ansi, ansi_c.x);
    bg_color = texture1D(ansi, ansi_c.y);
    
    texoffset.x = fract( scr_idx / txsz.x );          // this magically does not depend
    texoffset.y = floor( scr_idx / txsz.x ) / txsz.y; // on graphics' aspect ratio or size.             
    
    vec2 posn = pszar.xy*position*pszar.z - viewpoint;
     
    gl_Position = gl_ModelViewProjectionMatrix*vec4(posn.x, posn.y, 0, 1);
    gl_PointSize = pszar.z;    
}
   