#version 120
#line 2 0
#pragma optimize(off)
#pragma debug(on)

uniform sampler2D ansi;
uniform sampler2D font;
uniform float final_alpha;
uniform vec3 pszar;             // { Parx, Pary, Psz }
uniform vec4 txsz;              // { w_tiles, h_tiles, tile_w, tile_h }

// Total 8 uniform floats, 2 samplers

varying vec4 ansicolors;        // tile: computed foreground and background color indexes for tile and creature
varying vec4 tile;         	// creature: offset into font texture and tile size
varying vec4 creature;          // tile: offset into font texture and tile size

void main() { // final touch
    vec2 pc = gl_PointCoord/pszar.xy;
    if ((pc.x > 1.0) || (pc.y >1.0)) {
        discard;
    }
    vec4 texcoords = vec4 (tile.x + (pc.x/txsz.x)*tile.z,
                           tile.y + (pc.y/txsz.y)*tile.w,
                           creature.x + (pc.x/txsz.x)*creature.z,
                           creature.y + (pc.y/txsz.y)*creature.w);
    if (creature.z > 0.1) {
	vec4 cf_color = texture2D(ansi, vec2(ansicolors.z, 0));
	vec4 cb_color = texture2D(ansi, vec2(ansicolors.w, 0));
        vec4 crea_color = texture2D(font, texcoords.zw);
        gl_FragColor = mix(crea_color*cf_color, cb_color, 1-crea_color.a);
    } else {
	vec4 fg_color = texture2D(ansi, vec2(ansicolors.x, 0));
	vec4 bg_color = texture2D(ansi, vec2(ansicolors.y, 0));
	vec4 tile_color = texture2D(font, texcoords.xy);
	gl_FragColor = mix(tile_color*fg_color, bg_color, 1.0 - tile_color.a);
    }	
    gl_FragColor.a = final_alpha;
}
