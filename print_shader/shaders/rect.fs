#version 120
#line 2 0
#ifndef GL_ARB_texture_rectangle
#extension GL_ARB_texture_rectangle : require
#endif
#pragma optimize(off)
#pragma debug(on)

uniform sampler2DRect ansi;
uniform sampler2DRect font;
uniform float final_alpha;
uniform vec3 pszar;             // { Parx, Pary, Psz }
uniform vec4 txsz;              // { w_tiles, h_tiles, tile_w, tile_h }

// Total 4 uniform floats, 2 samplers

varying vec4 ansicolors;        // tile: computed foreground and background color indexes for tile and creature
varying vec4 tile;         	// creature: offset into font texture and tile size
varying vec4 creature;          // tile: offset into font texture and tile size

void main() { // final touch
    vec2 pc = gl_PointCoord/pszar.xy;
    if ((pc.x > 1.0) || (pc.y >1.0)) {
        discard;
    }
    vec4 texcoords = vec4 (pc.x*tile.z + tile.x,
                           pc.y*tile.w + tile.y,
                           pc.x*creature.z + creature.x,
                           pc.y*creature.w + creature.y );
    if (creature.z > 0.1) {
	vec4 cf_color = texture2DRect(ansi, vec2(ansicolors.z, 0));
	vec4 cb_color = texture2DRect(ansi, vec2(ansicolors.w, 0));
        vec4 crea_color = texture2DRect(font, texcoords.zw);
        gl_FragColor = mix(crea_color*cf_color, cb_color, 1-crea_color.a);
    } else {
	vec4 fg_color = texture2DRect(ansi, vec2(ansicolors.x, 0));
	vec4 bg_color = texture2DRect(ansi, vec2(ansicolors.y, 0));
	vec4 tile_color = texture2DRect(font, texcoords.xy);
	gl_FragColor = mix(tile_color*fg_color, bg_color, 1.0 - tile_color.a);
    }	
    gl_FragColor.a = final_alpha;
}
