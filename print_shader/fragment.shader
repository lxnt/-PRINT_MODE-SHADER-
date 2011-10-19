#version 120
#line 1 0

uniform sampler2D font;
uniform vec2 txsz;              // tile size in texels
uniform float final_alpha;
uniform vec3 pszar;             // { Parx, Pary, Psz }
// Total 6 uniform floats, 1 sampler

varying vec4 fg_color;          // tile: computed foreground color
varying vec4 bg_color;          // tile: computed background color
varying vec4 cf_color;          // creature: computed foreground color
varying vec4 cb_color;          // creature: computed background color
varying vec4 texoffset;         // tile and creature offsets into font texture

void main() { // final touch
    vec2 pc = gl_PointCoord/pszar.xy;
    if ((pc.x > 1.0) || (pc.y >1.0)) {
        discard;
    }
    vec4 texcoords = vec4 (pc.x/txsz.x + texoffset.x,
                           pc.y/txsz.y + texoffset.y,
                           pc.x/txsz.x + texoffset.z,
                           pc.y/txsz.y + texoffset.w );
    
    vec4 tile_color = texture2D(font, texcoords.xy);
    
    /* blend tile with its fg&bg colors */
    vec4 t_color = mix(tile_color*fg_color, bg_color, 1-tile_color.a);
    
    if (texcoords.z > 0) { // there is a creature 
        vec4 crea_color = texture2D(font, texcoords.zw);
        
        /* blend  creature with its fg&bg colors */
        //vec4 c_color = mix(crea_color*cf_color, cb_color, 1-crea_color.a);
    
        if (crea_color.a > 0.99) {
            gl_FragColor = mix(t_color, crea_color, 1-crea_color.a);
        }
    } else {
        gl_FragColor = t_color;
    }

    gl_FragColor.a = final_alpha;
}