extern GLchar _binary____fragment_shader_end;
extern GLchar _binary____fragment_shader_start;
extern GLchar _binary____vertex_shader_end;
extern GLchar _binary____vertex_shader_start;


extern texdumpst texdumper;
// #define BUFFER_OFFSET(i) ((char *)NULL + (i)) // BO offsets: do not need.

glsl_configst glsl_conf;

class renderer_glsl : public renderer {
	enum uniforms {
		FONT,
		ANSI,
		TXSZ,
		FINAL_ALPHA,
		PSZAR,
		VIEWPOINT,

		LASTUNIF
	};

	enum attrarrays {
		SCREEN,
		TEXPOS,
		ADDCOLOR,
		GRAYSCALE,
		CF,
		CBR,
		POSITION,

		LASTATTR
	};

	GLuint tex_id[2];
	GLuint vbo_id[LASTATTR];
	GLint  attr_loc[LASTATTR];
	GLint  unif_loc[LASTUNIF];
	GLuint shader;
	GLint txsz_w, txsz_h; 		// texture size in tiles
	GLint tile_w, tile_h;		// tile size in texels
	GLint texture_filter;
	GLfloat *grid;
	GLint grid_size;
	int Pszx, Pszy;	// Pointsize as drawn
	SDL_Surface *surface;
	uint32_t *screen_underlay;
	int f_counter;
	int viewport_offset_x, viewport_offset_y; // viewport tracking
	int viewport_w, viewport_h;               // for mouse coordinate transformation

	// configurable behavior
	bool do_snap_window;  		// snap window size to match viewport when zooming/resizing
	bool do_stretch_tiles; 		// deform tiles to fill whole viewport when zooming/resizing

	// internal flags
	bool do_reset_glcontext;    // if a full reset of opengl context is required after SDL_SetVideoMode()
	bool do_swap; 				// if SDL_GL_Swap() is needed.
	bool do_update_grid_vbo;    // if grid vbo was touched
	bool opengl_initialized;
	bool texture_ready;			// if we've got a suitable tileset/font texture to work with.

	void screen_underlay_reshape() {
		if (screen_underlay)
			delete [] screen_underlay;
		screen_underlay = new uint32_t[gps.dimx*gps.dimy];
		memmove(screen_underlay, gps.screen, gps.dimx*gps.dimy*4);
	}
	void screen_underlay_update() {
		for (int i=0; i<gps.dimx*gps.dimy; i++)
			if (!gps.screentexpos[i])
				screen_underlay[i] = *((Uint32 *)gps.screen + i);
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
	void makegrid() {
		int w = gps.dimx;
		int h = gps.dimy;
		std::cerr << "makegrid(): "<<w<<"x"<<h<<"\n";
		grid_size = sizeof(GLfloat) * w * h * 2;
		grid = static_cast<GLfloat*> (realloc(grid, grid_size));
		int i = 0;
		for (int xt = 0; xt < w; xt++)
			for (int yt = 0; yt < h; yt++) {
				GLfloat x = xt + 0.5;
				GLfloat y = h - yt - 0.5;
				grid[2 * i + 0] = x;
				grid[2 * i + 1] = y;
				i++;
			}
		do_update_grid_vbo = true;
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
		glBindTexture(GL_TEXTURE_1D, tex_id[ANSI]);
		glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 16, 0, GL_RGBA, GL_FLOAT, ansi_stuff);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		fprintf(stderr, "makeansitex(): %d.\n", tex_id[ANSI]);
	}
	void texture_reset() {
		SDL_Surface *cats = texdumper.get();
		if (!cats) {
			texture_ready = false;
			return; // just skip if there's nothing to use
		}
	    glMatrixMode(GL_TEXTURE);
	    glLoadIdentity();
	    glMatrixMode(GL_MODELVIEW);
		fputsGLError(stderr);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, tex_id[FONT]);
		fputsGLError(stderr);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cats->w, cats->h,
				0, GL_RGBA, GL_UNSIGNED_BYTE, cats->pixels);
		fputsGLError(stderr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		fputsGLError(stderr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		fputsGLError(stderr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture_filter);
		fputsGLError(stderr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_filter);
		fputsGLError(stderr);
		bool reshape_required = (  (tile_w != texdumper.t_w) || (tile_h != texdumper.t_h) );
		txsz_w = texdumper.w_t;
		txsz_h = texdumper.h_t;
		tile_w = texdumper.t_w;
		tile_h = texdumper.t_h;
		glUniform2f(unif_loc[TXSZ], txsz_w, txsz_h);
		fputsGLError(stderr);
		fprintf(stderr, "accepted font texture (name=%d): %dx%dpx oa\n",
				tex_id[FONT], cats->w, cats->h);
		if (glsl_conf.dump_screen > 0)
			texdumper.dump();
		texture_ready = true;
		if (reshape_required)
			reshape();
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
		} else {
			fprintf(stderr, "Using embedded vertex shader code.\n");
			v_len = &_binary____vertex_shader_end - &_binary____vertex_shader_start;
			v_src = &_binary____vertex_shader_start;
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
		} else {
			fprintf(stderr, "Using embedded fragment shader code.\n");
			f_len = &_binary____fragment_shader_end - &_binary____fragment_shader_start;
			f_src = &_binary____fragment_shader_start;
		}
		const GLchar * v_srcp[1] = { v_src };
		const GLchar * f_srcp[1] = { f_src };

		GLuint v_sh = glCreateShader(GL_VERTEX_SHADER);
		GLuint f_sh = glCreateShader(GL_FRAGMENT_SHADER);
		shader = glCreateProgram();
		fputsGLError(stderr);

		glAttachShader(shader, v_sh);
		glAttachShader(shader, f_sh);
		fputsGLError(stderr);

		glShaderSource(v_sh, 1, v_srcp, &v_len);
		glShaderSource(f_sh, 1, f_srcp, &f_len);
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

		unif_loc[FONT] = glGetUniformLocation(shader, "font");
		unif_loc[ANSI] = glGetUniformLocation(shader, "ansi");
		unif_loc[TXSZ] = glGetUniformLocation(shader, "txsz");
		unif_loc[FINAL_ALPHA] = glGetUniformLocation(shader, "final_alpha");
		unif_loc[PSZAR] = glGetUniformLocation(shader, "pszar");
		unif_loc[VIEWPOINT] = glGetUniformLocation(shader, "viewpoint");
		fputsGLError(stderr);

		attr_loc[SCREEN] = glGetAttribLocation(shader, "screen");
		attr_loc[TEXPOS] = glGetAttribLocation(shader, "texpos");
		attr_loc[ADDCOLOR] = glGetAttribLocation(shader, "addcolor");
		attr_loc[GRAYSCALE] = glGetAttribLocation(shader, "grayscale");
		attr_loc[CF] = glGetAttribLocation(shader, "cf");
		attr_loc[CBR] = glGetAttribLocation(shader, "cbr");
		attr_loc[POSITION] = glGetAttribLocation(shader, "position");
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

		glUniform1i(unif_loc[ANSI], 0); 		// GL_TEXTURE0 : ansi color strip
		glUniform1i(unif_loc[FONT], 1); 		// GL_TEXTURE1 : font
		glUniform1f(unif_loc[FINAL_ALPHA], 1.0);
		fputsGLError(stderr);
		/* note: TXSZ and POINTSIZE/PAR are not bound yet. */
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
		glClear( GL_COLOR_BUFFER_BIT);
		fputsGLError(stderr);
	}
	void opengl_init() {
		glewInit();
		glGenTextures(2, tex_id);
		glGenBuffers(LASTATTR, vbo_id);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_NOTEQUAL, 0);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glEnable(GL_POINT_SPRITE);
		fputsGLError(stderr);
		shader_setup();
		fputsGLError(stderr);
		makeansitex();
		fputsGLError(stderr);
		texture_reset();
		fputsGLError(stderr);
		opengl_initialized = true;
	}
	void opengl_fini() {
		glDeleteProgram(shader);
		glDeleteTextures(2, tex_id);
		texture_ready = false;
		glDeleteBuffers(7, vbo_id);
		fputsGLError(stderr);
		opengl_initialized = false;
	}
	void update_vbos() {
		int tiles = gps.dimx * gps.dimy;
		screen_underlay_update();
		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[SCREEN]);
		fputsGLError(stderr);
//		glBufferData(GL_ARRAY_BUFFER, tiles * 4, gps.screen, GL_DYNAMIC_DRAW);
		glBufferData(GL_ARRAY_BUFFER, tiles * 4, screen_underlay, GL_DYNAMIC_DRAW);
		fputsGLError(stderr);
		glVertexAttribPointer(attr_loc[SCREEN], 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		fputsGLError(stderr);

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[TEXPOS]);
		glBufferData(GL_ARRAY_BUFFER, tiles * 4, gps.screentexpos, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[TEXPOS], 1, GL_UNSIGNED_INT, GL_FALSE, 0, 0);
		fputsGLError(stderr);

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[ADDCOLOR]);
		glBufferData(GL_ARRAY_BUFFER, tiles, gps.screentexpos_addcolor, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[ADDCOLOR], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		fputsGLError(stderr);

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[GRAYSCALE]);
		glBufferData(GL_ARRAY_BUFFER, tiles, gps.screentexpos_grayscale, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[GRAYSCALE], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		fputsGLError(stderr);

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[CF]);
		glBufferData(GL_ARRAY_BUFFER, tiles, gps.screentexpos_cf, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[CF], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		fputsGLError(stderr);

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[CBR]);
		glBufferData(GL_ARRAY_BUFFER, tiles, gps.screentexpos_cbr, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[CBR], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		fputsGLError(stderr);

		if (do_update_grid_vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, vbo_id[POSITION]);
			fputsGLError(stderr);
			glBufferData(GL_ARRAY_BUFFER, grid_size, grid, GL_STATIC_DRAW);
			fputsGLError(stderr);
			glVertexAttribPointer(attr_loc[POSITION], 2, GL_FLOAT, GL_FALSE, 0, 0);
			fputsGLError(stderr);
			do_update_grid_vbo = false;
		}
	}
	/** reshape(): this function clamps grid size as needed and then resets grid, pointsizes
	 *  and window size/video mode/fullscreen as dictated by the reshape policy.
	 *  @param new_grid_x, new_grid_y - new requested grid size
	 *  @param new_window_w, new_window_h - new requested window size
	 *  @param toggle_fullscreen - if a toggle of fullscreen state was requested
	 */
	void reshape(int new_grid_w = 0, int new_grid_h = 0, int new_window_w = -1, int new_window_h = -1,
					bool toggle_fullscreen = false) {
		fprintf(stderr, "reshape(): got grid %dx%d window %dx%d texture_ready=%d stretch=%d snap=%d\n",
				new_grid_w, new_grid_h, new_window_w, new_window_h,
				texture_ready, do_stretch_tiles, do_snap_window);

		if (!texture_ready) // can't draw anything without tile_w, tile_h and a texture anyway
			return;

		/* set new window size to current if default arguments are default */
		if (new_window_w < 0)
			new_window_w = surface->w;
		if (new_window_h < 0)
			new_window_h = surface->h;

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

		/* Attempt to stuff some more tiles on the screen by enlarging grid
		 * unless we're zooming in since that would be counterproductive.  */
		if ( (!enabler.overridden_grid_sizes.size())
			   && (new_grid_w > gps.dimx)
			   && (new_grid_h > gps.dimy) ) {
			new_grid_w = new_window_w/new_psz_x;
			new_grid_h = new_window_h/new_psz_y;

			/* but clamp in case we're too optimistic */
			new_grid_w = MIN(MAX(new_grid_w, MIN_GRID_X), MAX_GRID_X);
			new_grid_h = MIN(MAX(new_grid_h, MIN_GRID_Y), MAX_GRID_Y);
		}

		if (do_stretch_tiles) {
			/* now try to fill rest  of the window with graphics,
			 * not paying any more attention to tile graphics aspect ratio
			 */
			new_psz_x = new_window_w / new_grid_w;
			new_psz_y = new_window_h / new_grid_h;
		}

		/* okay, we can now set Psz/Parx/Pary to their new values */
		Pszx = new_psz_x;
		Pszy = new_psz_y;

		if (Pszx > Pszy) {
			GLfloat Parx = 1.0;
			GLfloat Pary = (double)new_psz_y / (double)new_psz_x;
			glUniform3f(unif_loc[PSZAR], Parx, Pary, Pszx);
		} else {
			GLfloat Parx = (double)new_psz_x / (double)new_psz_y;
			GLfloat Pary = 1.0;
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
		if ((new_grid_w != gps.dimx) || (new_grid_h != gps.dimy)) {
			gps_allocate(new_grid_w, new_grid_h);
			makegrid();
			screen_underlay_reshape();
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
				if (do_snap_window) { // set window size to viewport size.
					set_mode(viewport_w, viewport_h, false);
					do_snap_window = false; // so it doesn't get used when zooming
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
	void zoom(double zoom) { // in - negative; out - positive
		double g_ar = (double) gps.dimx / gps.dimy;
		int new_grid_w = g_ar * zoom + gps.dimx;
		int new_grid_h = zoom / g_ar + gps.dimy;

		/* check if resulting Psz isn't excessively insane */
		if (surface->w * surface->h / (new_grid_w * new_grid_h) < 4) {
			fprintf(stderr, "Zoom canceled: grid %dx%d psz %.02fx%0.2f",
					new_grid_w, new_grid_h,
					(double)surface->w/new_grid_w,
					(double)surface->h/new_grid_h );
			return;
		}

		reshape(new_grid_w, new_grid_h);
	}

	void resize(int new_window_w, int new_window_h, bool toggle_fullscreen = false) {
		/* here so we don't duplicate code for the fullscreen case */
		if (   (new_window_w == surface->w)
			&& (new_window_h == surface->h)
			&& !toggle_fullscreen)
			return;

		int new_grid_w, new_grid_h;
		if (enabler.overridden_grid_sizes.size()) {
			new_grid_w = gps.dimx;
			new_grid_h = gps.dimy;
		} else {
			// approximately preserve existing zoom
			new_grid_w = new_window_w / ( surface->w / gps.dimx );
			new_grid_h = new_window_h / ( surface->h / gps.dimy );
		}
		do_snap_window = glsl_conf.snap_window;
		reshape(new_grid_w, new_grid_h, new_window_w, new_window_h, toggle_fullscreen);
	}

	void dump_screen(const char *fname) {
		std::ofstream f;
		const int dimx = init.display.grid_x;
		const int dimy = init.display.grid_y;

		f.open(fname, std::ios::app | std::ios::binary);
		f.write((char *)(&dimx), sizeof(dimx));
		f.write((char *)(&dimy), sizeof(dimy));
		f.write((char *)(&surface->w), sizeof(surface->w));
		f.write((char *)(&surface->h), sizeof(surface->h));
		f.write((char *)(screen), 4*dimx*dimy);
		f.write((char *)(screentexpos), sizeof(long)*dimx*dimy);
		f.write((char *)(screentexpos_addcolor), dimx*dimy);
		f.write((char *)(screentexpos_grayscale), dimx*dimy);
		f.write((char *)(screentexpos_cf), dimx*dimy);
		f.write((char *)(screentexpos_cbr), dimx*dimy);
		f.write((char *)(screentexpos_cbr), dimx*dimy);
		f.write((char *)(screen_underlay), 4*dimx*dimy);
		f.close();
	}

public:
	virtual void display() 					{ if (0) std::cerr<<"display(): do not need.\n"; }
	virtual void update_tile(int x, int y)  { if (1) std::cerr<<"update_tile(): do not need.\n"; }
	virtual void update_all() 				{ if (1) std::cerr<<"update_all(): do not need.\n"; }
	virtual void render() {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_1D, tex_id[ANSI]);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, tex_id[FONT]);
		glUniform1f(unif_loc[FINAL_ALPHA], 1.0);
		glClearColor(0.0, 0.5, 0.0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		update_vbos();
		glDrawArrays(GL_POINTS, 0, gps.dimx * gps.dimy);
		fputsGLError(stderr);
		if (do_swap)
			SDL_GL_SwapBuffers();
		f_counter ++;
		if ((glsl_conf.dump_screen > 0) && (f_counter % glsl_conf.dump_screen == 0))
			dump_screen("screendump");
	}
	virtual void set_fullscreen() 			{ zoom(zoom_fullscreen); }
	//rtual void swap_arrays() 				{ if (0) std::cerr<<"swap_arrays(): do not need.\n"; }

	virtual void zoom(zoom_commands cmd) {
		switch (cmd) {
			case zoom_in:
				if (enabler.overridden_grid_sizes.size())
					return;
				zoom(-init.input.zoom_speed);
				break;
			case zoom_out:
				if (enabler.overridden_grid_sizes.size())
					return;
				zoom(init.input.zoom_speed);
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
				do_snap_window = glsl_conf.snap_window;
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
				do_snap_window = glsl_conf.snap_window;
				resize(new_window_w, new_window_h, true);
				return;
		}
	}
	virtual void grid_resize(int new_grid_w, int new_grid_h) {
		/* dis gets called from enablerst::override_grid_size() only
		 * this means the grid size is fixed. thus do not touch it,
		 *
		 * do_snap_window gets reset every reshape to not be
		 * taken into account when zooming (what a crappy design)
		 */
		do_snap_window = glsl_conf.snap_window;
		reshape(new_grid_w, new_grid_h);
	}
	virtual void resize(int w, int h) {
		/* dis gets called on SDL_VIDEORESIZE event */
		do_snap_window = glsl_conf.snap_window;
		resize(w, h, false);
	}

	renderer_glsl() {
		f_counter = 0;
		texture_ready = false;
		tile_w = 0;
		tile_h = 0;
		do_snap_window = glsl_conf.snap_window;
		do_stretch_tiles = !init.display.flag.has_flag(INIT_DISPLAY_FLAG_BLACK_SPACE);
		texture_filter = init.window.flag.has_flag(INIT_WINDOW_FLAG_TEXTURE_LINEAR) ? GL_LINEAR : GL_NEAREST;

		char sdl_videodriver[256];
		if (NULL == SDL_VideoDriverName(sdl_videodriver, sizeof(sdl_videodriver)))
			/* wtf?? SDL_Init has not been called */
			return;

		do_reset_glcontext =  (strncmp(sdl_videodriver, "x11", 3) != 0);

		SDL_EnableKeyRepeat(0, 0); // Disable key repeat
		SDL_WM_SetCaption(GAME_TITLE_STRING, NULL); // Set window title/icon.
		SDL_Surface *icon = IMG_Load("data/art/icon.png");
		if (icon != NULL) {
			SDL_WM_SetIcon(icon, NULL);
			SDL_FreeSurface(icon);
		}

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
				; // now, wtf we're going to do?
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
	}
	virtual bool get_mouse_coords(int &x, int &y) {
		int mouse_x, mouse_y;
		SDL_GetMouseState(&mouse_x, &mouse_y);
		x = (mouse_x - viewport_offset_x) / (Pszx * gps.dimx);
		y = (mouse_y - viewport_offset_y) / (Pszy * gps.dimy);
		return true;
	}
	virtual bool uses_opengl() {
		bool very_true = true;
		return true && very_true;
	}
};
