#include "zlib.h"

#include "shaders.c"
void dfapi_init(void);
void fugr_dump(void);

extern texdumpst texdumper;
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

glsl_configst glsl_conf;
const GLsizeiptr sizeof_tile = 4 + 4 + 4 + 1 + 1 + 1 + 1;
const GLsizeiptr sizeof_screen = MAX_GRID_X * MAX_GRID_Y * sizeof_tile;
const GLsizeiptr sizeof_vertex = 2 * sizeof(GLfloat);
const GLsizeiptr sizeof_grid = MAX_GRID_X * MAX_GRID_Y * sizeof_vertex;

class renderer_glsl : public renderer {
    enum uniforms {
        FONT_SAMPLER,
        ANSI_SAMPLER,
        TXCO_SAMPLER,
        TXSZ,
        FINAL_ALPHA,
        PSZAR,
        VIEWPOINT,

        LAST_UNIF
    };
    enum attrarrays {
        SCREEN,
        TEXPOS,
        ADDCOLOR,
        GRAYSCALE,
        CF,
        CBR,
        POSITION,
        TEXCOORDS,

        LAST_ATTR
    };
    enum buffers {
        ULOD_BO,
        POSITION_BO,

        LAST_BO
    };
    enum textures {
        FONT,
        ANSI,
        TXCO,

        LAST_TEX
    };

    GLuint tex_id[LAST_TEX];
    GLuint vbo_id[LAST_BO];
    GLint  attr_loc[LAST_ATTR];
    GLint  unif_loc[LAST_UNIF];
    GLuint shader;
    GLint txsz_w, txsz_h; 		// texture size in tiles
    GLint cell_w, cell_h;		// texture cell size in texels (actual graphic may be smaller)
    GLint tile_w, tile_h;		// font texture cell size in texels (may be smaller than the above)
    GLfloat *grid;
    GLsizeiptr grid_bo_size;	// size of currently used BO
    GLint grid_tile_count; 		// and in tiles
    GLint grid_w, grid_h;		// and again in tiles
    int Pszx, Pszy, Psz;		// Point sprite size as drawn
    SDL_Surface *surface;
    Uint32 last_frame_start_ts; // when render() for the last frame was called.
    Uint32 last_frame_swap_ts;  // when render() for the last frame finished.
    GLfloat *tilesizes;         // tile sizes: a texture. dimensions eq txsz_w x txsz_h.
    int texture_generation;     // screen dumper uses this
    void *dump_buffer;          // its internals

    unsigned char *screen;      // ULoD: an ugly lump of data.
    unsigned char *_screen_unaligned;
    GLsizeiptr screen_bo_size;
    struct _bo_offset {
        GLsizei screen; // should be 0 at all times, but ...
        GLsizei underlay;
        GLsizei texpos;
        GLsizei addcolor;
        GLsizei grayscale;
        GLsizei cf;
        GLsizei cbr;
    } bo_offset;

    int f_counter;							  // frame counter
    int viewport_offset_x, viewport_offset_y; // viewport tracking
    int viewport_w, viewport_h;               // for mouse coordinate transformation

    // internal flags
    bool do_reset_glcontext;    // if a full reset of opengl context is required after SDL_SetVideoMode()
    bool do_swap; 				// if SDL_GL_Swap() is needed.
    bool do_sync; 				// if glFenceSync() is to be used.
    bool do_update_attrs;       // if grid vbo was touched: that is, reshaped.
    bool opengl_initialized;
    bool texture_ready;			// if we've got a suitable tileset/font texture to work with.
    bool reset_underlay;		// if underlay has to be reset, i.e. there was scrolling.

    Uint32 last_seen_ul, last_seen_crea;

    Uint32 fadein_finish_at;	// when fadein should finish (abs sdltick)
    Uint32 print_rendertime_at;

    inline Uint32 *tile_u32(int x, int y, int s) { return ((Uint32 *) (screen + s) + x*grid_h + y ); };
    inline Uint32 *index_u32(int i, int s) { return ((Uint32 *) (screen + s) + i ); };
    inline Uint8 *index_u8(int i, int s) { return ((Uint8 *) (screen + s) + i ); };

    /** screen_underlay. Contains a copy of screen with tiles that are
     * now under creatures not overwritten, if possible.
     * For that, just before next render_things (where game internals overwrite stuff in ULoD),
     * we copy previous frame screen data to the underlay if there was no creature on the tile.
     *
     * Now, if in the next frame creature moved to this tile, we have something to render under it.
     *
     * This breaks on scrolling, because underlay no longer matches next frame screen.
     *
     * This also does not work when creature does not move wrt the screen, as in adventure mode,
     * where screen is centered on the hero most of the time, while view scrolls as he moves.
     *
     * However in df mode, this mostly works, because there's less scrolling, and dwarves
     * tend to run around a lot, or blink in/out if standing on a slope or something like that.
     *
     */
    void screen_underlay_update() {
        if (reset_underlay) {
            memmove(screen + bo_offset.underlay, screen, 4*grid_tile_count);
            reset_underlay = false;
            return;
        }
        for (int i=0; i < grid_tile_count; i++)
            if (! *index_u32(i, bo_offset.texpos) ) // no creature here
                *index_u32(i, bo_offset.underlay) = *index_u32(i, bo_offset.screen);

            else {
                if (glsl_conf.dump_creatures)
                    record_crea(i);
#ifdef DEBUG_CREABLEND_TRACK
                if ( *index_u32(i, bo_offset.underlay) != *index_u32(i, bo_offset.screen) ) {
                    Uint32 crea = *index_u32(i, bo_offset.underlay);
                    Uint32 val = *index_u32(i, bo_offset.screen);
                    if ((val != last_seen_ul) || (crea != last_seen_crea)) {
                        unsigned char *gp = (unsigned char *)index_u32(i, bo_offset.screen);
                        unsigned char *ul = (unsigned char *)index_u32(i, bo_offset.underlay);
                        fprintf(stderr, "crid %04x@%04x: %02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x\n",
                                *index_u32(i, bo_offset.texpos), i,
                                ul[0], ul[1], ul[2], ul[3],
                                gp[0], gp[1], gp[2], gp[3] );
                        last_seen_ul = val;
                        last_seen_crea = crea;
                    }
                }
#endif
            }
    }
    void record_crea(int i) {
        char c,g;
        long texpos = *(long*)index_u32(i, bo_offset.texpos);

        std::set<long>::iterator it;
        it = texdumper.cloned.find(i);
        c = (it == texdumper.cloned.end()) ? ' ' : 'c';
        it = texdumper.gray.find(i);
        g = (it == texdumper.gray.end()) ? ' ' : 'g';
        unsigned char *s = (unsigned char *) index_u32(i, bo_offset.screen);
        fprintf(stderr, "%02x.%02x.%02x.%02x texpos %5ld [%c%c] %1x:%1x:%1x:%1x\n",
                s[0], s[1], s[2], s[3],
                texpos, c, g,
                *index_u8(i, bo_offset.addcolor),
                *index_u8(i, bo_offset.grayscale),
                *index_u8(i, bo_offset.cf),
                *index_u8(i, bo_offset.cbr)
        );

    }
    virtual void gps_allocate(int x, int y) {
        fprintf(stderr, "gps_allocate(%d, %d)\n", x, y);
        ulod_allocate(x, y);
        grid_allocate(x, y);
        gps.resize(x, y);
        do_update_attrs = true;
    }
    void ulod_allocate(int x, int y) {
        if (!_screen_unaligned) {
            _screen_unaligned = (unsigned char *) malloc(sizeof_screen + 4096);
            /* make sure the ULoD starts on page boundary. */
            ptrdiff_t screen_v = (ptrdiff_t)_screen_unaligned;
            int page_align = ( 0x1000 - ( screen_v & 0xFFF));
            screen = _screen_unaligned + page_align;
        }

        screen_bo_size = sizeof_tile*x*y;

        memset(screen, 0, screen_bo_size);

        bo_offset.screen                = x*y*(0);
        bo_offset.underlay              = x*y*(4);
        bo_offset.texpos                = x*y*(4+4);
        bo_offset.addcolor              = x*y*(4+4+4);
        bo_offset.grayscale             = x*y*(4+4+4+1);
        bo_offset.cf                    = x*y*(4+4+4+1+1);
        bo_offset.cbr                   = x*y*(4+4+4+1+1+1);

        gps.screen                      = (unsigned char *) ( screen + bo_offset.screen );
        gps.screentexpos                = (         long *) ( screen + bo_offset.texpos );
        gps.screentexpos_addcolor       = (         char *) ( screen + bo_offset.addcolor );
        gps.screentexpos_grayscale      = (unsigned char *) ( screen + bo_offset.grayscale );
        gps.screentexpos_cf             = (unsigned char *) ( screen + bo_offset.cf );
        gps.screentexpos_cbr            = (unsigned char *) ( screen + bo_offset.cbr );
    }
    bool set_mode(int w, int h, bool fullscreen) {
        Uint32 flags = SDL_OPENGL;

        bool vsync = init.window.flag.has_flag(INIT_WINDOW_FLAG_VSYNC_ON);
        bool singlebuf = init.display.flag.has_flag(INIT_DISPLAY_FLAG_SINGLE_BUFFER);
        bool noresize = init.display.flag.has_flag(INIT_DISPLAY_FLAG_NOT_RESIZABLE);
        bool fullscreen_state = (surface != NULL ) && (surface->flags & SDL_FULLSCREEN);
        bool resolution_change = (surface == NULL) || (surface->w != w) || (surface->h != h);
        if (	opengl_initialized
            && ( ( fullscreen_state && fullscreen )
            || ( ! ( fullscreen_state || fullscreen ) ) ) // xor :)
            && (! resolution_change ) ) // nothing to do here, move along
            return true;

        if (opengl_initialized && do_reset_glcontext)
            opengl_fini();

        if (fullscreen) {
            flags |= SDL_FULLSCREEN;
        } else {
            if (not noresize)
                flags |= SDL_RESIZABLE;
        }

        SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, vsync);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, not singlebuf);

        std::cerr<<"set_mode(): requesting vsync="<<vsync<<" and singlebuf="<<singlebuf<<".\n";

        if (!(surface = SDL_SetVideoMode(w, h, 32, flags))) {
            report_error("SDL_SetVideoMode", SDL_GetError());
            return false;
        }
        int test;
        SDL_GL_GetAttribute(SDL_GL_SWAP_CONTROL, &test);
        std::cerr<<"set_mode(): SDL_GL_SWAP_CONTROL: "<<(bool)test<<"\n";
        SDL_GL_GetAttribute(SDL_GL_ACCELERATED_VISUAL, &test);
        std::cerr<<"set_mode(): SDL_GL_ACCELERATED_VISUAL: "<<(bool)test<<"\n";
        SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &test);
        do_swap = (bool)test;
        std::cerr<<"set_mode(): SDL_GL_DOUBLEBUFFER: "<<do_swap<<"\n";
        fullscreen_state = surface->flags & SDL_FULLSCREEN;
        std::cerr<<"set_mode(): SDL_FULLSCREEN: "<<fullscreen_state<<"\n";

        if ( do_swap and singlebuf ) {
            if (fullscreen_state)
                std::cerr<<"set_mode(): requested single-buffering, failed, not caring because of fullscreen.\n";
            else
                report_error("OpenGL","Requested single-buffering not available");
        }

        if (!opengl_initialized)
            opengl_init();

        return true;
    }
    void grid_allocate(int w, int h) {
        grid_bo_size =  w * h * sizeof_vertex;
        if (!grid)
            grid = (GLfloat *) malloc(sizeof_grid);
        int i = 0;
        for (int xt = 0; xt < w; xt++)
            for (int yt = 0; yt < h; yt++) {
                GLfloat x = xt + 0.5;
                GLfloat y = h - yt - 0.5;
                grid[2 * i + 0] = x;
                grid[2 * i + 1] = y;
                i++;
            }
        grid_w = w;
        grid_h = h;
        grid_tile_count = grid_w*grid_h;
    }
    void makeansitex(void) {
        GLfloat ansi_stuff[16 * 4];
        for (int i = 0; i < 16; i++) {
            ansi_stuff[4 * i + 0] = enabler.ccolor[i][0];
            ansi_stuff[4 * i + 1] = enabler.ccolor[i][1];
            ansi_stuff[4 * i + 2] = enabler.ccolor[i][2];
            ansi_stuff[4 * i + 3] = 1.0;
        }
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_id[ANSI]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_FLOAT, ansi_stuff);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        fprintf(stderr, "makeansitex(): %d.\n", tex_id[ANSI]);
    }
    void texture_reset() {
        if (!texdumper.raws) {
            texture_ready = false;
            return; // just skip if there's nothing to use
        }
        texdumper.update();
        SDL_Surface *cats = texdumper.cats;

        glActiveTexture(GL_TEXTURE1);
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        fputsGLError(stderr);

        glBindTexture(GL_TEXTURE_2D, tex_id[FONT]);
        fputsGLError(stderr);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cats->w, cats->h,
                0, GL_RGBA, GL_UNSIGNED_BYTE, cats->pixels);
        fputsGLError(stderr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        fputsGLError(stderr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        fputsGLError(stderr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glsl_conf.texture_filter);
        fputsGLError(stderr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glsl_conf.texture_filter);
        fputsGLError(stderr);

        glActiveTexture(GL_TEXTURE2);
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        fputsGLError(stderr);

        glBindTexture(GL_TEXTURE_2D, tex_id[TXCO]);
        fputsGLError(stderr);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texdumper.w_t, texdumper.h_t,
                0, GL_RGBA, GL_UNSIGNED_BYTE, texdumper.tile_sizes);
        fputsGLError(stderr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        fputsGLError(stderr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        fputsGLError(stderr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        fputsGLError(stderr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        fputsGLError(stderr);

        bool reshape_required = (  (tile_w != texdumper.t1_w) || (tile_h != texdumper.t1_h) );
        txsz_w = texdumper.w_t;
        txsz_h = texdumper.h_t;
        tile_w = texdumper.t1_w;
        tile_h = texdumper.t1_h;
        cell_w = texdumper.max_tw;
        cell_h = texdumper.max_th;
        glUniform4f(unif_loc[TXSZ], txsz_w, txsz_h, cell_w, cell_h);
        fputsGLError(stderr);
        fprintf(stderr, "accepted font texture (name=%d): %dx%dpx oa\n", tex_id[FONT], cats->w, cats->h);
        fprintf(stderr, "accepted txco texture (name=%d): %dx%dpx oa\n", tex_id[TXCO], txsz_w, txsz_h);
        if (glsl_conf.dump_stuff > 0)
            texdumper.dump(glsl_conf.dump_pfx.c_str(), texture_generation);
        texture_generation ++;
        texture_ready = true;
        if (reshape_required)
            reshape();
        fadein_start();
    }
    void rebind_textures() {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_id[ANSI]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex_id[FONT]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, tex_id[TXCO]);
    }
    bool shader_status(GLuint fsvs, GLenum pname) {
        GLint param;
        glGetShaderiv(fsvs, pname, &param);
        const char *tru = param == GL_TRUE ? "true" : "false";
        switch (pname) {
        case GL_COMPILE_STATUS:
            std::cerr << "GL_COMPILE_STATUS: " << tru << "\n";
            break;
        case GL_INFO_LOG_LENGTH:
            break;
        default:
            return param;
        }
        if ((pname == GL_INFO_LOG_LENGTH) || (param == GL_FALSE)) {
            if (pname != GL_INFO_LOG_LENGTH)
                glGetShaderiv(fsvs, GL_INFO_LOG_LENGTH, &param);
            std::cerr << "infolog of " << param << " bytes\n";

            if (param > 0) {
                char *infolog = new char[param];
                glGetShaderInfoLog(fsvs, param, &param, infolog);
                std::cerr << "ShaderInfoLog:\n" << infolog << "\n";
            }
        }
        return param;
    }
    bool glprog_status(GLenum pname) {
        GLint param;
        glGetProgramiv(shader, pname, &param);
        const char *tru = param == GL_TRUE ? "true" : "false";
        switch (pname) {
        case GL_DELETE_STATUS:
            std::cerr << "GL_DELETE_STATUS: " << tru << "\n";
            break;
        case GL_LINK_STATUS:
            std::cerr << "GL_LINK_STATUS: " << tru << "\n";
            break;
        case GL_VALIDATE_STATUS:
            std::cerr << "GL_VALIDATE_STATUS: " << tru << "\n";
            break;
        case GL_INFO_LOG_LENGTH:
            break;
        default:
            return param;
        }
        if ((pname == GL_INFO_LOG_LENGTH) || (param == GL_FALSE)) {
            if (pname != GL_INFO_LOG_LENGTH)
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &param);
            std::cerr << "infolog of " << param << " bytes\n";
            if (param > 0) {
                char *infolog = new char[param];
                glGetProgramInfoLog(shader, param, &param, infolog);
                std::cerr << "ProgramInfoLog:\n" << infolog << "\n";
            }
        }
        return param;
    }
    void shader_setup() {
        GLint v_len, f_len;
        GLchar *v_src, *f_src;
        std::ifstream f;
        _shader_pair *e_shaset = shader_collection+1;
        for (_shader_pair *sp = e_shaset; sp->name != NULL; sp++) {
            fprintf(stderr, "Embedded shader set '%s'%s\n",
                    sp->name, (glsl_conf.shader_set == sp->name) ? e_shaset = sp, " x" : "");
        }

        f.open(glsl_conf.vs_path.c_str(), ios::binary);
        if (f.is_open()) {
            fprintf(stderr, "Using external vertex shader code from '%s'.\n", glsl_conf.vs_path.c_str());
            std::ifstream f(glsl_conf.vs_path.c_str(), ios::binary);
            v_len = f.seekg(0, std::ios::end).tellg();
            f.seekg(0, ios::beg);
            v_src = new GLchar[v_len + 1];
            f.read(v_src, v_len);
            f.close();
            v_src[v_len] = 0;
            shader_collection[0].v_len[0] = v_len;
            shader_collection[0].v_src[0] = v_src;
        } else {
            fprintf(stderr, "Using embedded vertex shader code from '%s'.\n", e_shaset->name);
            shader_collection[0].v_len[0] = e_shaset->v_len[0];
            shader_collection[0].v_src[0] = e_shaset->v_src[0];
        }
        f.open(glsl_conf.fs_path.c_str(), ios::binary);
        if (f.is_open()) {
            fprintf(stderr, "Using external fragment shader code from '%s'.\n", glsl_conf.fs_path.c_str());
            std::ifstream f(glsl_conf.fs_path.c_str(), ios::binary);
            f_len = f.seekg(0, ios::end).tellg();
            f.seekg(0, ios::beg);
            f_src = new GLchar[f_len + 1];
            f.read(f_src, f_len);
            f.close();
            f_src[f_len] = 0;
            shader_collection[0].f_len[0] = f_len;
            shader_collection[0].f_src[0] = f_src;
        } else {
            fprintf(stderr, "Using embedded fragment shader code from '%s'.\n", e_shaset->name);
            shader_collection[0].f_len[0] = e_shaset->f_len[0];
            shader_collection[0].f_src[0] = e_shaset->f_src[0];
        }

        GLuint v_sh = glCreateShader(GL_VERTEX_SHADER);
        GLuint f_sh = glCreateShader(GL_FRAGMENT_SHADER);
        shader = glCreateProgram();
        fputsGLError(stderr);

        glAttachShader(shader, v_sh);
        glAttachShader(shader, f_sh);
        fputsGLError(stderr);

        glShaderSource(v_sh, 1, const_cast<const GLchar**>(shader_collection[0].v_src), shader_collection[0].v_len);
        glShaderSource(f_sh, 1, const_cast<const GLchar**>(shader_collection[0].f_src), shader_collection[0].f_len);
        fputsGLError(stderr);

        glCompileShader(v_sh);
        if (!shader_status(v_sh, GL_COMPILE_STATUS))
            exit(1);

        glCompileShader(f_sh);
        if (!shader_status(f_sh, GL_COMPILE_STATUS))
            exit(1);

        fputsGLError(stderr);

        glLinkProgram(shader);
        if (!glprog_status(GL_LINK_STATUS))
            exit(1);

        glValidateProgram(shader);
        if (!glprog_status(GL_VALIDATE_STATUS))
            exit(1);

        glUseProgram(shader);
        fputsGLError(stderr);

        glDeleteShader(v_sh); // mark for deletion
        glDeleteShader(f_sh);
        fputsGLError(stderr);

        makeansitex();


        unif_loc[ANSI_SAMPLER]	= glGetUniformLocation(shader, "ansi");
        unif_loc[FONT_SAMPLER]	= glGetUniformLocation(shader, "font");
        unif_loc[TXCO_SAMPLER]	= glGetUniformLocation(shader, "txco");
        unif_loc[TXSZ]			= glGetUniformLocation(shader, "txsz");
        unif_loc[FINAL_ALPHA]   = glGetUniformLocation(shader, "final_alpha");
        unif_loc[PSZAR] 	    = glGetUniformLocation(shader, "pszar");
        unif_loc[VIEWPOINT]     = glGetUniformLocation(shader, "viewpoint");
        fputsGLError(stderr);

        attr_loc[SCREEN] 		= glGetAttribLocation(shader, "screen");
        attr_loc[TEXPOS] 		= glGetAttribLocation(shader, "texpos");
        attr_loc[ADDCOLOR] 		= glGetAttribLocation(shader, "addcolor");
        attr_loc[GRAYSCALE] 	= glGetAttribLocation(shader, "grayscale");
        attr_loc[CF] 			= glGetAttribLocation(shader, "cf");
        attr_loc[CBR] 			= glGetAttribLocation(shader, "cbr");
        attr_loc[POSITION] 		= glGetAttribLocation(shader, "position");
        fputsGLError(stderr);

        glEnableVertexAttribArray(attr_loc[SCREEN]);
        fputsGLError(stderr);
        glEnableVertexAttribArray(attr_loc[TEXPOS]);
        fputsGLError(stderr);
        glEnableVertexAttribArray(attr_loc[ADDCOLOR]);
        fputsGLError(stderr);
        glEnableVertexAttribArray(attr_loc[GRAYSCALE]);
        fputsGLError(stderr);
        glEnableVertexAttribArray(attr_loc[CF]);
        fputsGLError(stderr);
        glEnableVertexAttribArray(attr_loc[CBR]);
        fputsGLError(stderr);
        glEnableVertexAttribArray(attr_loc[POSITION]);
        fputsGLError(stderr);

        glUniform1i(unif_loc[ANSI_SAMPLER], 0); 		// GL_TEXTURE0 : ansi color strip
        glUniform1i(unif_loc[FONT_SAMPLER], 1); 		// GL_TEXTURE1 : font
        glUniform1i(unif_loc[TXCO_SAMPLER], 2); 		// GL_TEXTURE2 : tile sizes
        glUniform1f(unif_loc[FINAL_ALPHA], 1.0);
        glUniform2f(unif_loc[VIEWPOINT], 0.0, 0.0);
        fputsGLError(stderr);
        /* note: TXSZ and POINTSIZE/PAR are not bound yet. */
    }
    void reload_shaders() {
        glDeleteProgram(shader); // frees all the stuff
        shader_setup();
        do_update_attrs = true;
        glUniform4f(unif_loc[TXSZ], txsz_w, txsz_h, cell_h, cell_w);
        fputsGLError(stderr);
        reshape(grid_w, grid_h); // update PSZAR
		fugr_dump();
    }
    void set_viewport() {
        fprintf(stderr, "set_viewport(): got %dx%d out of %dx%d\n",
                viewport_w, viewport_h, surface->w, surface->h);
        viewport_offset_x = (surface->w - viewport_w)/2;
        viewport_offset_y = (surface->h - viewport_h)/2;
        glMatrixMode( GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(0, viewport_w, 0, viewport_h);
        glViewport(viewport_offset_x, viewport_offset_y, viewport_w, viewport_h);
        glMatrixMode( GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0.3, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        fputsGLError(stderr);
    }
    void opengl_vital_parameters() {
        struct {
            GLenum pname;
            const char *name;
        } _gls[] = {
                { GL_VENDOR, "vendor" },
                { GL_RENDERER, "renderer" },
                { GL_VERSION, "version" },
                { GL_SHADING_LANGUAGE_VERSION, "GLSL version" },
                {  },

        }, *gls = _gls;
        while (gls->name)
            fprintf(stderr, "OpenGL %s: %s\n", gls->name, glGetString( gls->pname)), gls++;

        struct {
            GLint needed;
            GLenum pname;
            const char *name;
        } _vp[] = {
                {  7, GL_MAX_VERTEX_ATTRIBS, "GL_MAX_VERTEX_ATTRIBS" }, // number of vec4 attribs available
                {  9, GL_MAX_VERTEX_UNIFORM_COMPONENTS, "GL_MAX_VERTEX_UNIFORM_COMPONENTS" }, // single-component values
                { 11, GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS" }, // same as above
                {  0, GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS" }, // samplers in vert shader
                {  3, GL_MAX_TEXTURE_IMAGE_UNITS, "GL_MAX_TEXTURE_IMAGE_UNITS" },  // samplers in frag shader
                {  8, GL_MAX_VARYING_FLOATS, "GL_MAX_VARYING_FLOATS" }, // 4 varying_floats = 1 texture_coord?
                {  2, GL_MAX_TEXTURE_COORDS, "GL_MAX_TEXTURE_COORDS" }, // 1 texture_coord = 4 varying_floats?
                { -4, GL_POINT_SIZE_MIN, "GL_POINT_SIZE_MIN" },
                { 48, GL_POINT_SIZE_MAX, "GL_POINT_SIZE_MAX" }, // no idea of our requirements
                {  },
        }, *vp = _vp;
        while (vp->name) {
            GLint param;
            glGetIntegerv(vp->pname, &param);
            if ((param < vp->needed) ||((param<0) && (param+vp->needed) > 0))
                fputs("** ", stderr);
            fprintf(stderr, "%s=%d, needed=%d\n", vp->name, param, vp->needed < 0 ? -vp->needed : vp->needed);
            vp++;
        }
        fprintf(stderr, "GL_ARB_sync: %s\n", glewGetExtension("GL_ARB_sync") ? "supported" : "not supported");
    }
    void opengl_init() {
        GLenum err = glewInit();
        if (GLEW_OK != err) {
            fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
            exit(1);
        }

        fprintf(stderr, "GLEW: %s\n", glewGetString(GLEW_VERSION));

        opengl_vital_parameters();
        if ( !GLEW_VERSION_2_0 ) {
            fprintf(stderr, "OpenGL 2.0 or later is required .\n");
            exit(1);
        }

        glGenTextures(LAST_TEX, tex_id);
        glGenBuffers(LAST_BO, vbo_id);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_NOTEQUAL, 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_POINT_SPRITE);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glDisable(GL_POINT_SMOOTH);
        GLint param = GL_UPPER_LEFT;
        glPointParameteriv(GL_POINT_SPRITE_COORD_ORIGIN, &param);

        makeansitex();
        texture_reset();

        fputsGLError(stderr);
        shader_setup();
        fputsGLError(stderr);

        do_sync = init.display.flag.has_flag(INIT_DISPLAY_FLAG_ARB_SYNC) && glewGetExtension("GL_ARB_sync");
        opengl_initialized = true;
    }
    void opengl_fini() {
        glDeleteProgram(shader);
        glDeleteTextures(LAST_TEX, tex_id);
        texture_ready = false;
        glDeleteBuffers(LAST_BO, vbo_id);
        fputsGLError(stderr);
        opengl_initialized = false;
        do_update_attrs = true;
    }
    void update_vbos() {
        if (glsl_conf.use_underlay)
            screen_underlay_update();

        glBindBuffer(GL_ARRAY_BUFFER, vbo_id[ULOD_BO]);
        fputsGLError(stderr);

        glBufferData(GL_ARRAY_BUFFER, screen_bo_size, screen, GL_STREAM_DRAW);
        fputsGLError(stderr);

        if (do_update_attrs) {
            if (glsl_conf.use_underlay)
                glVertexAttribPointer(attr_loc[SCREEN], 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, BUFFER_OFFSET(bo_offset.underlay));
            else
                glVertexAttribPointer(attr_loc[SCREEN], 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, BUFFER_OFFSET(bo_offset.screen));
            fputsGLError(stderr);

            glVertexAttribPointer(attr_loc[TEXPOS], 1, GL_UNSIGNED_INT, GL_FALSE, 0, BUFFER_OFFSET(bo_offset.texpos));
            fputsGLError(stderr);
            glVertexAttribPointer(attr_loc[ADDCOLOR], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, BUFFER_OFFSET(bo_offset.addcolor));
            fputsGLError(stderr);
            glVertexAttribPointer(attr_loc[GRAYSCALE], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, BUFFER_OFFSET(bo_offset.grayscale));
            fputsGLError(stderr);
            glVertexAttribPointer(attr_loc[CF], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, BUFFER_OFFSET(bo_offset.cf));
            fputsGLError(stderr);
            glVertexAttribPointer(attr_loc[CBR], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, BUFFER_OFFSET(bo_offset.cbr));
            fputsGLError(stderr);

            glBindBuffer(GL_ARRAY_BUFFER, vbo_id[POSITION_BO]);
            fputsGLError(stderr);
            glBufferData(GL_ARRAY_BUFFER, grid_bo_size, grid, GL_STATIC_DRAW);
            fputsGLError(stderr);
            glVertexAttribPointer(attr_loc[POSITION], 2, GL_FLOAT, GL_FALSE, 0, 0);
            fputsGLError(stderr);
            do_update_attrs = false;
        }
    }
    /** reshape(): this function clamps grid size as needed and then resets grid, pointsizes
     *  and window size/video mode/fullscreen as dictated by the reshape policy.
     *  @param new_grid_x, new_grid_y - new requested grid size
     *  @param new_window_w, new_window_h - new requested window size
     *  @param toggle_fullscreen - if a toggle of fullscreen state was requested
     */
    void reshape(int new_grid_w = 0, int new_grid_h = 0, int new_window_w = -1, int new_window_h = -1,
            bool toggle_fullscreen = false, bool override_snap = false) {
        fprintf(stderr, "reshape(): got grid %dx%d window %dx%d tile %dx%d texture_ready=%d stretch=%d snap=%d\n",
                new_grid_w, new_grid_h, new_window_w, new_window_h, tile_w, tile_h,
                texture_ready, glsl_conf.stretch_tiles, glsl_conf.snap_window);

        if (!texture_ready) // can't draw anything without tile_w, tile_h and a texture anyway
            return;

        /* set new window size to current if default arguments are default */
        if (new_window_w < 0)
            new_window_w = surface->w;
        if (new_window_h < 0)
            new_window_h = surface->h;

        if (Psz < 0) { //der kludge for the first-time reshape triggered by the first texture_reset()
            new_grid_w = new_window_w/tile_w;
            new_grid_h = new_window_h/tile_h;
        }

        /*  clamp requested grid size */
        new_grid_w = MIN(MAX(new_grid_w, MIN_GRID_X), MAX_GRID_X);
        new_grid_h= MIN(MAX(new_grid_h, MIN_GRID_Y), MAX_GRID_Y);

        int new_psz_x, new_psz_y;

        /* Attempt to preserve tile graphics aspect ratio
         * by not paying attention if viewport will be considerably
         * smaller than the window.
         */
        double fx = new_window_w / ((double)new_grid_w * (double) tile_w);
        double fy = new_window_h / ((double)new_grid_h * (double) tile_h);
        double ff = MIN(fx, fy);

        /* interim new tile sizes */
        new_psz_x = ff * tile_w;
        new_psz_y = ff * tile_h;

        Psz = MAX(new_psz_x, new_psz_y);

        /* Attempt to stuff some more tiles on the screen by enlarging grid
         * unless we're zooming in since that would be counterproductive.  */
        if ( (!enabler.overridden_grid_sizes.size())
                && (new_grid_w > grid_w)
                && (new_grid_h > grid_h) ) {
            new_grid_w = new_window_w/new_psz_x;
            new_grid_h = new_window_h/new_psz_y;

            /* but clamp in case we're too optimistic */
            new_grid_w = MIN(MAX(new_grid_w, MIN_GRID_X), MAX_GRID_X);
            new_grid_h = MIN(MAX(new_grid_h, MIN_GRID_Y), MAX_GRID_Y);
        }

        if (glsl_conf.stretch_tiles) {
            /* now try to fill rest  of the window with graphics,
             * not paying any more attention to tile graphics aspect ratio
             */
            new_psz_x = new_window_w / new_grid_w;
            new_psz_y = new_window_h / new_grid_h;
        }

        /* okay, we can now set Psz/Parx/Pary to their new values */
        Pszx = new_psz_x;
        Pszy = new_psz_y;

        GLfloat Parx = 1.0, Pary = 1.0;
        if (Pszx > Pszy) {
            Pary = (double)new_psz_y / (double)new_psz_x;
            glUniform3f(unif_loc[PSZAR], Parx, Pary, Pszx);
        } else {
            Parx = (double)new_psz_x / (double)new_psz_y;
            glUniform3f(unif_loc[PSZAR], Parx, Pary, Pszy);
        }
        /* viewport is the size of drawn grid in pixels, or alternatively,
         * the area in window that is actually drawn to
         */

        viewport_w = new_psz_x * new_grid_w;
        viewport_h = new_psz_y * new_grid_h;

        fprintf(stderr, "reshape(): final grid %dx%d window %dx%d viewport %dx%d Psz %dx%d\n",
                new_grid_w, new_grid_h, new_window_w, new_window_h,
                viewport_w, viewport_h, Pszx, Pszy );

        /* reshape grid if that is needed */
        if ((new_grid_w != grid_w) || (new_grid_h != grid_h)) {
            gps_allocate(new_grid_w, new_grid_h);
        }

        bool fullscreen = surface->flags & SDL_FULLSCREEN; // are we currently in fullscreen mode?

        if (toggle_fullscreen) {
            if (fullscreen) { // we're switching fullscreen off
                set_mode(new_window_w, new_window_h, false);
            } else { // we're switching fullscreen on
                set_mode(new_window_w, new_window_h, true);
            }
        } else {
            if (!fullscreen) {
                if (glsl_conf.snap_window && !override_snap) { // set window size to viewport size.
                    set_mode(viewport_w, viewport_h, false);
                } else { // ah, whatever.
                    set_mode(new_window_w, new_window_h, false);
                }
            } else {
                ; // we're in fullscreen and viewport changed due to zoom
            }
        }
        set_viewport();
    }
    /** calculates new grid
     * Zoom policy: we must maintain grid aspect ratio.
     * Tile aspect ratio is handled in reshape()
     */
    void zoom(int zoom) { // in - negative; out - positive
        int new_psz = Psz + zoom;
        if (new_psz < 2) // don't be ridiculous
            return;

        double t_ar = (double) tile_w / tile_h;

        int new_psz_x =( tile_w > tile_h ? 1.0 : t_ar ) * new_psz;
        int new_psz_y =( tile_w > tile_h ? 1.0/t_ar : 1.0 ) * new_psz;

        int new_grid_w = surface->w/new_psz_x;
        int new_grid_h = surface->h/new_psz_y;

        reshape(new_grid_w, new_grid_h, -1, -1, false, true);
    }
    void resize(int new_window_w, int new_window_h, bool toggle_fullscreen = false) {
        /* here so we don't duplicate code for the fullscreen case */
        if (   (new_window_w == surface->w)
                && (new_window_h == surface->h)
                && !toggle_fullscreen)
            return;

        int new_grid_w, new_grid_h;
        if (enabler.overridden_grid_sizes.size()) {
            new_grid_w = grid_w;
            new_grid_h = grid_h;
        } else {
            if (Psz < MAX(tile_w, tile_h)) {
                // zoom in until tiles are pixel-for-pixel
                Psz = MAX(tile_w, tile_h);
                new_grid_w = new_window_w / tile_w;
                new_grid_h = new_window_h / tile_h;
            } else {
                // approximately preserve existing zoom
                new_grid_w = new_window_w / ( surface->w / grid_w );
                new_grid_h = new_window_h / ( surface->h / grid_h );
            }
        }
        reshape(new_grid_w, new_grid_h, new_window_w, new_window_h, toggle_fullscreen);
    }
    void dump_screen() {
        char fname[4096];
        snprintf(fname, 4096, "%s.sdump", glsl_conf.dump_pfx.c_str());

        if (!dump_buffer)
            dump_buffer = malloc(sizeof_screen);

        struct _dump_header {
            struct _bo_offset bo_offset;
            size_t data_len; // compressed data size that follows this struct
            GLint grid_w, grid_h;		// and again in tiles
            int Pszx, Pszy, Psz;			// Point sprite size as drawn
            int viewport_offset_x, viewport_offset_y; // viewport tracking
            int viewport_w, viewport_h;               // for mouse coordinate transformation
            int surface_w, surface_h;		// window dimensions
            GLint txsz_w, txsz_h; 		// texture size in tiles
            GLint tile_w, tile_h;		// tile size in texels
            GLint cell_w, cell_h;		// tile size in texels
            int frame_number;
            int texture_generation;    // which of texdumpNNNN.png the above refer to
        } hdr;

        memmove(&hdr.bo_offset, &bo_offset, sizeof(struct _bo_offset));
        hdr.grid_w = grid_w; hdr.grid_h = grid_h;
        hdr.Pszx = Pszx; hdr.Pszy = Pszy;  hdr.Psz = Psz;
        hdr.surface_w = surface->w; hdr.surface_h = surface->h;
        hdr.txsz_w = txsz_w; hdr.txsz_h = txsz_h;
        hdr.tile_w = tile_w; hdr.tile_h = tile_h;
        hdr.cell_w = cell_w; hdr.cell_h = cell_h;
        hdr.texture_generation = texture_generation - 1;
        hdr.frame_number = f_counter;

        uLongf destLen = sizeof_screen;
        uLong sourceLen = screen_bo_size;
        int z_ok = compress((unsigned char *)dump_buffer, &destLen, screen,  sourceLen);
        if (z_ok != Z_OK) {
            fprintf(stderr, "compress2(): %d\n", z_ok);
            return;
        }
        hdr.data_len = destLen;
        std::ofstream f(fname, std::ios::app | std::ios::binary);
        f.write((char *)(&hdr), sizeof(struct _dump_header));
        f.write((char *)(dump_buffer), destLen);
        f.close();
        fprintf(stderr, "dump_screen(): frame %d: %ld bytes (compressed %ld->%ld + %d)\n",
                f_counter, destLen + sizeof(struct _dump_header), sourceLen, destLen, sizeof(struct _dump_header));
    }
    void fadein_start() {
        if (glsl_conf.fade_in > 0) {
            fadein_finish_at = SDL_GetTicks() + glsl_conf.fade_in;
            glUniform1f(unif_loc[FINAL_ALPHA], 0.0);
        } else {
            glUniform1f(unif_loc[FINAL_ALPHA], 1.0);
        }
    }
    void fadein_update(Uint32 now) {
        if (fadein_finish_at > now) {
            glUniform1f(unif_loc[FINAL_ALPHA], 1.0 - (double)(fadein_finish_at - now)/(double) glsl_conf.fade_in);
        } else {
            fadein_finish_at = 0;
            glUniform1f(unif_loc[FINAL_ALPHA], 1.0);
        }
    }
    void timed_stuff(Uint32 now, Uint32 last_st, Uint32 last_sw) {
        if (last_st == 0) {
            fadein_start();
            return; // first frame
        }
        if (fadein_finish_at > 0)
            fadein_update(now);
        if (print_rendertime_at < now) {
            print_rendertime_at = now + 5000;
            fprintf(stderr, "Frame drawn in %d msec\n", last_sw - last_st);
        }
    }
public:
    virtual void display() 					{ if (0) std::cerr<<"display(): do not need.\n"; }
    virtual void update_tile(int x, int y)  { if (1) std::cerr<<"update_tile(): do not need.\n"; }
    virtual void update_all() 				{ reload_shaders(); } // ugly overload :)
    virtual void render() {
        Uint32 start_ts = SDL_GetTicks();
        timed_stuff(start_ts, last_frame_start_ts, last_frame_swap_ts);

#if 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_1D, tex_id[ANSI]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex_id[FONT]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_1D, tex_id[TXCO]);
        //glUniform1f(unif_loc[FINAL_ALPHA], 1.0);
#endif
        glClearColor(
                enabler.ccolor[glsl_conf.glclearcolor][0],
                enabler.ccolor[glsl_conf.glclearcolor][1],
                enabler.ccolor[glsl_conf.glclearcolor][2],
                1.0 );
        glClear(GL_COLOR_BUFFER_BIT);
        fputsGLError(stderr);
        update_vbos();
        rebind_textures();
        glDrawArrays(GL_POINTS, 0, grid_tile_count);
        fputsGLError(stderr);
        if (do_sync)
            enabler.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        if (do_swap)
            SDL_GL_SwapBuffers(); // unconditional glXSwapBuffers; does implicit glFlush
        else
            glFlush();

        last_frame_start_ts = start_ts;
        last_frame_swap_ts = SDL_GetTicks();
        f_counter ++;
        if ((glsl_conf.dump_stuff > 0) && (f_counter % glsl_conf.dump_stuff == 0))
            dump_screen();
    }
    virtual void set_fullscreen() 			{ zoom(zoom_fullscreen); }
    virtual void swap_arrays() 				{ screen_underlay_update(); }

    virtual void zoom(zoom_commands cmd) {
        switch (cmd) {
        case zoom_in:
            if (enabler.overridden_grid_sizes.size())
                return;
            zoom(-1);
            break;
        case zoom_out:
            if (enabler.overridden_grid_sizes.size())
                return;
            zoom(1);
            break;
        case zoom_reset:
            if (enabler.overridden_grid_sizes.size())
                return;
            // set 1:1
            reshape(surface->w/tile_w, surface->h/tile_h);
            break;
        case zoom_resetgrid:
            /* intended to reset grid size to previous overriden size
             * but this does not work atm (see comments in sender).
             * So just set some grid size.
             */
            fprintf(stderr, "zoom(): zoom_resetgrid\n");
            reshape(surface->w/tile_w, surface->h/tile_h);
            break;
        case zoom_fullscreen:
            /* received when toggling fullscreen.
             * enabler.is_fullscreen() is true if we're requested to set fullscreen mode
             * otherwise we're requested to set windowed mode
             */
            int new_window_w, new_window_h;

            if (enabler.is_fullscreen()) {
                if ( surface->flags & SDL_FULLSCREEN) {
                    fprintf(stderr, "zoom(): Fullscreen mode requested, but we're already in it.\n");
                    return;
                }
                init.display.desired_windowed_width = surface->w;
                init.display.desired_windowed_height = surface->h;
                new_window_w = init.display.desired_fullscreen_width;
                new_window_h = init.display.desired_fullscreen_height;
            } else {
                if (!( surface->flags & SDL_FULLSCREEN)) {
                    fprintf(stderr, "Windowed mode requested, but we're already in it.\n");
                    return;
                }
                new_window_w = init.display.desired_windowed_width;
                new_window_h = init.display.desired_windowed_height;
            }
            resize(new_window_w, new_window_h, true);
            return;
        }
    }
    virtual void grid_resize(int new_grid_w, int new_grid_h) {
        /* dis gets called from enablerst::override_grid_size() only
         * this means the grid size is fixed. thus do not touch it,
         */
        reshape(new_grid_w, new_grid_h);
    }
    virtual void resize(int w, int h) {
        /* dis gets called on SDL_VIDEORESIZE event */
        resize(w, h, false);
    }

    renderer_glsl() {
        f_counter = 0;
        texture_ready = false;
        tile_w = 0;
        tile_h = 0;
        cell_w = 0;
        cell_h = 0;
        grid_w = 0;
        grid_h = 0;
        grid = NULL;
        _screen_unaligned = NULL;
        texture_generation = 0;
        dump_buffer = NULL;
        surface = NULL;
        last_frame_start_ts = 0;
        print_rendertime_at = 0;
        fadein_finish_at = 0;
        Psz = -8;
        opengl_initialized = false;
		
		dfapi_init();

        char sdl_videodriver[256];
        if (NULL == SDL_VideoDriverName(sdl_videodriver, sizeof(sdl_videodriver)))
            exit(EXIT_FAILURE);

        do_reset_glcontext =  (strncmp(sdl_videodriver, "x11", 3) != 0);

        SDL_EnableKeyRepeat(0, 0); // Disable key repeat
        SDL_WM_SetCaption(GAME_TITLE_STRING, NULL); // Set window title/icon.
        SDL_Surface *icon = IMG_Load("data/art/icon.png");
        if (icon != NULL) {
            SDL_WM_SetIcon(icon, NULL);
            SDL_FreeSurface(icon);
        }
        do_sync = false;

        if (init.display.desired_fullscreen_width == 0
                || init.display.desired_fullscreen_height == 0) {
            SDL_Rect **modes = SDL_ListModes(NULL, SDL_OPENGL|SDL_FULLSCREEN);
            /* avoid setting fullscreen over all monitors if there's no
             * user input on this <- needs more thought */
            SDL_Rect *goodmode = NULL;
            if (modes == NULL) {
                // okay, no fullscreen opengl here
                enabler.fullscreen = false;
            }
            if (modes == (SDL_Rect **)-1)
                8; // now, wtf we're going to do? what's ANY mode?
            else {
                goodmode = modes[0];
                for (int i=1; modes[i]; ++i) {
                    if ((goodmode->w/modes[i]->w < 2) && (goodmode->w/modes[i]->w < 2))
                        break;
                    goodmode = modes[i];
                }

                init.display.desired_fullscreen_width = goodmode->w;
                init.display.desired_fullscreen_height = goodmode->h;
            }
        }

        // Initialize our window
        bool worked = set_mode(
                enabler.is_fullscreen() ? init.display.desired_fullscreen_width
                        : init.display.desired_windowed_width,
                          enabler.is_fullscreen() ? init.display.desired_fullscreen_height
                                  : init.display.desired_windowed_height,
                                    enabler.is_fullscreen() );

        // Fallback to windowed mode if fullscreen fails
        if (!worked && enabler.is_fullscreen()) {
            enabler.fullscreen = false; // FIXME: access violation: refactor enablerst
            report_error("SDL initialization failure, trying windowed mode",
                    SDL_GetError());
            worked = set_mode(init.display.desired_windowed_width,
                    init.display.desired_windowed_height, false);
        }
        // Quit if windowed fails
        if (!worked) {
            report_error("SDL initialization failure", SDL_GetError());
            exit(EXIT_FAILURE);
        }
        gps_allocate(MIN_GRID_X, MIN_GRID_Y);
        Pszx = surface->w/MIN_GRID_X; // so that get_mousecoords don't
        Pszy = surface->h/MIN_GRID_Y; // divide by zero.
    }
    virtual bool get_mouse_coords(int &x, int &y) {
        int mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        x = (mouse_x - viewport_offset_x) / Pszx;
        y = (mouse_y - viewport_offset_y) / Pszy;
        return true;
    }
    virtual bool uses_opengl() {
        bool very_true = true;
        return true && very_true;
    }
    virtual ~renderer_glsl() {
        if (grid) free(grid);
        if (screen) free(screen);
        if (dump_buffer) free(dump_buffer);
    }
};
