#version 120
#line 2 0

uniform sampler2D ansi;
uniform sampler2D font;
uniform sampler2D txco;
uniform float final_alpha;
uniform vec3 pszar;             // { Parx, Pary, Psz }
uniform vec4 txsz;              // { w_tiles, h_tiles, tile_w, tile_h }

// Total 8 uniform floats, 3 samplers

varying vec4 ansicolors;        // tile: computed foreground and background color indexes for tile and creature
varying vec2 tilecrea;         	// floor and creature tile indexes

// Total 6 float varyings (8 on Mesa)

vec4 idx2texco(float idx) {
    vec4 tile_size;
    vec4 rv;
    
    if (idx < 0) 				// no creature 
    	return vec4(0,0,0,0);
    
    rv.x = fract( idx / txsz.x );  	    // normalized coords 
    rv.y = floor( idx / txsz.x ) / txsz.y;  // into font texture - "offset"

    tile_size = texture2D(txco, rv.xy);
    rv.zw = tile_size.xy*(256.0/txsz.zw); // pixel size of the tile normalized to maxtilesize
    
    return rv;
}

void main() {
    vec2 pc = gl_PointCoord/pszar.xy;
    if ((pc.x > 1.0) || (pc.y >1.0)) {
        discard;
    }
    vec4 tile = idx2texco(tilecrea.x);
    vec4 creature = idx2texco(tilecrea.y);
    
    vec4 texcoords = vec4 (tile.x + (pc.x/txsz.x)*tile.z,
                           tile.y + (pc.y/txsz.y)*tile.w,
                           creature.x + (pc.x/txsz.x)*creature.z,
                           creature.y + (pc.y/txsz.y)*creature.w);

    vec4 cf_color = texture2D(ansi, vec2(ansicolors.z, 0.0));
    vec4 cb_color = texture2D(ansi, vec2(ansicolors.w, 0.0));
    vec4 crea_color = texture2D(font, texcoords.zw);
    vec4 fg_color = texture2D(ansi, vec2(ansicolors.x, 0.0));
    vec4 bg_color = texture2D(ansi, vec2(ansicolors.y, 0.0));
    vec4 tile_color = texture2D(font, texcoords.xy);
    if (tilecrea.y > 0) // no creatures with idx==0 atm.
        gl_FragColor = mix(crea_color*cf_color, cb_color, 1.0 - crea_color.a);
    else 
		gl_FragColor = mix(tile_color*fg_color, bg_color, 1.0 - tile_color.a);
    gl_FragColor.a = final_alpha;
}
