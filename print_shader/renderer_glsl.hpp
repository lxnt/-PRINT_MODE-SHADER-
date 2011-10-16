// SHADER
extern GLchar _binary____fragment_shader_end;
extern GLchar _binary____fragment_shader_start;
extern GLchar _binary____vertex_shader_end;
extern GLchar _binary____vertex_shader_start;


extern texdumpst texdumper;
#define BUFFER_OFFSET(i) ((char *)NULL + (i))
long do_dump_screen = 0;
char *vs_path = NULL;
char *fs_path = NULL;
class renderer_glsl : public renderer {
	enum uniforms {
		FONT,
		ANSI,
		TXSZ,
		FINAL_ALPHA,
		POINTSIZE,
		VIEWPOINT,
		PAR,

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
	bool do_swap, snap_window;
	GLfloat *grid;
	GLfloat Psz, Parx, Pary;	// Pointsize, PointAspectX, PointAspectY
	SDL_Surface *surface;
	uint32_t *screen_underlay;
	int f_counter;

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
	bool set_mode(int w, int h) {
		Uint32 flags = SDL_OPENGL | SDL_HWSURFACE;

		bool vsync = init.window.flag.has_flag(INIT_WINDOW_FLAG_VSYNC_ON);
		bool singlebuf = init.display.flag.has_flag(INIT_DISPLAY_FLAG_SINGLE_BUFFER);
		bool noresize = init.display.flag.has_flag(INIT_DISPLAY_FLAG_NOT_RESIZABLE);

		if (enabler.is_fullscreen()) {
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


		if ( do_swap and singlebuf ) {
			if (enabler.is_fullscreen())
				std::cerr<<"set_mode(): requested single-buffering, failed, not caring because of fullscreen.\n";
			else
				report_error("OpenGL","Requested single-buffering not available");
		}
		opengl_setmode();
		return true;
	}
	void makegrid() {
		std::cerr << "makegrid(): "<<gps.dimx<<"x"<<gps.dimy<<"\n";
		int size = sizeof(GLfloat) * gps.dimx * gps.dimy * 2;
		grid = static_cast<GLfloat*> (realloc(grid, size));
		int i = 0;
		for (int xt = 0; xt < gps.dimx; xt++)
			for (int yt = 0; yt < gps.dimy; yt++) {
				GLfloat x = xt + 0.5;
				GLfloat y = gps.dimy - yt - 0.5;
				grid[2 * i + 0] = x;
				grid[2 * i + 1] = y;
				i++;
			}
		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[POSITION]);
		printGLError();
		glBufferData(GL_ARRAY_BUFFER, size, grid, GL_STATIC_DRAW);
		printGLError();
		glVertexAttribPointer(attr_loc[POSITION], 2, GL_FLOAT, GL_FALSE, 0, 0);
		printGLError();
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
	    glMatrixMode(GL_TEXTURE);
	    glLoadIdentity();
	    glMatrixMode(GL_MODELVIEW);
		printGLError();
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, tex_id[FONT]);
		printGLError();
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cats->w, cats->h,
				0, GL_RGBA, GL_UNSIGNED_BYTE, cats->pixels);
		printGLError();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		printGLError();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		printGLError();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture_filter);
		printGLError();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_filter);
		printGLError();
		txsz_w = texdumper.w_t;
		txsz_h = texdumper.h_t;
		tile_w = texdumper.t_w;
		tile_h = texdumper.t_h;
		glUniform2f(unif_loc[TXSZ], txsz_w, txsz_h);
		printGLError();
		fprintf(stderr, "accepted font texture (name=%d): %dx%dpx oa\n",
				tex_id[FONT], cats->w, cats->h);
		if (do_dump_screen > 0)
			texdumper.dump();
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
		if (vs_path) {
			fprintf(stderr, "Using external vertex shader code: '%s'.\n", vs_path);
			std::ifstream f(vs_path, ios::binary);
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
		if (fs_path) {
			fprintf(stderr, "Using external fragment shader code: '%s'.\n", fs_path);
			std::ifstream f(fs_path, ios::binary);
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
		printGLError();

		glAttachShader(shader, v_sh);
		glAttachShader(shader, f_sh);
		printGLError();

		glShaderSource(v_sh, 1, v_srcp, &v_len);
		glShaderSource(f_sh, 1, f_srcp, &f_len);
		printGLError();

		glCompileShader(v_sh);
		if (!shader_status(v_sh, GL_COMPILE_STATUS))
			exit(1);
		glCompileShader(f_sh);
		if (!shader_status(f_sh, GL_COMPILE_STATUS))
			exit(1);
		printGLError();

		glLinkProgram(shader);
		if (!glprog_status(GL_LINK_STATUS))
			exit(1);

		glValidateProgram(shader);
		if (!glprog_status(GL_VALIDATE_STATUS))
			exit(1);

		glUseProgram(shader);
		printGLError();

		unif_loc[FONT] = glGetUniformLocation(shader, "font");
		unif_loc[ANSI] = glGetUniformLocation(shader, "ansi");
		unif_loc[TXSZ] = glGetUniformLocation(shader, "txsz");
		unif_loc[FINAL_ALPHA] = glGetUniformLocation(shader, "final_alpha");
		unif_loc[POINTSIZE] = glGetUniformLocation(shader, "pointsize");
		unif_loc[VIEWPOINT] = glGetUniformLocation(shader, "viewpoint");
		unif_loc[PAR] = glGetUniformLocation(shader, "par");
		printGLError();

		attr_loc[SCREEN] = glGetAttribLocation(shader, "screen");
		attr_loc[TEXPOS] = glGetAttribLocation(shader, "texpos");
		attr_loc[ADDCOLOR] = glGetAttribLocation(shader, "addcolor");
		attr_loc[GRAYSCALE] = glGetAttribLocation(shader, "grayscale");
		attr_loc[CF] = glGetAttribLocation(shader, "cf");
		attr_loc[CBR] = glGetAttribLocation(shader, "cbr");
		attr_loc[POSITION] = glGetAttribLocation(shader, "position");
		printGLError();

		glEnableVertexAttribArray(attr_loc[SCREEN]);
		printGLError();
		glEnableVertexAttribArray(attr_loc[TEXPOS]);
		printGLError();
		glEnableVertexAttribArray(attr_loc[ADDCOLOR]);
		printGLError();
		glEnableVertexAttribArray(attr_loc[GRAYSCALE]);
		printGLError();
		glEnableVertexAttribArray(attr_loc[CF]);
		printGLError();
		glEnableVertexAttribArray(attr_loc[CBR]);
		printGLError();
		fprintf(stderr, "%d", attr_loc[POSITION]);
		glEnableVertexAttribArray(attr_loc[POSITION]);
		printGLError();

		glUniform1i(unif_loc[ANSI], 0); 		// GL_TEXTURE0 : ansi color strip
		glUniform1i(unif_loc[FONT], 1); 		// GL_TEXTURE1 : font
		glUniform1f(unif_loc[FINAL_ALPHA], 1.0);
		printGLError();
		/* note: TXSZ and POINTSIZE/PAR are not bound yet. */
	}
	void opengl_setmode() {
		glMatrixMode( GL_PROJECTION);
		glLoadIdentity();
		gluOrtho2D(0, surface->w, 0, surface->h);
		glViewport(0, 0, surface->w, surface->h);
		glMatrixMode( GL_MODELVIEW);
		glLoadIdentity();
		glClearColor(0.3, 0.0, 0.0, 1.0);
		glClear( GL_COLOR_BUFFER_BIT);
	}
	void opengl_init() {
		glewInit(); // crashes on windows? Muahahaha
		glGenTextures(2, tex_id);
		glGenBuffers(LASTATTR, vbo_id);
		opengl_setmode();
		glEnable( GL_ALPHA_TEST);
		glAlphaFunc(GL_NOTEQUAL, 0);
		glEnable( GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable( GL_DEPTH_TEST);
		glDepthMask( GL_FALSE);
		glEnable( GL_POINT_SPRITE_ARB);
		printGLError();

		shader_setup();
		makeansitex();
	}
	void opengl_fini() {
		std::cerr<<"Oh noes! gl-fini!\n";
		glDeleteProgram(shader);
		glDeleteTextures(2, tex_id);
		glDeleteBuffers(7, vbo_id);
	}
	void update_vbos() {
		int tiles = gps.dimx * gps.dimy;
		screen_underlay_update();
		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[SCREEN]);
		printGLError();
//		glBufferData(GL_ARRAY_BUFFER, tiles * 4, gps.screen, GL_DYNAMIC_DRAW);
		glBufferData(GL_ARRAY_BUFFER, tiles * 4, screen_underlay, GL_DYNAMIC_DRAW);
		printGLError();
		glVertexAttribPointer(attr_loc[SCREEN], 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		printGLError();

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[TEXPOS]);
		glBufferData(GL_ARRAY_BUFFER, tiles * 4, gps.screentexpos, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[TEXPOS], 1, GL_UNSIGNED_INT, GL_FALSE, 0, 0);
		printGLError();

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[ADDCOLOR]);
		glBufferData(GL_ARRAY_BUFFER, tiles, gps.screentexpos_addcolor, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[ADDCOLOR], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		printGLError();

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[GRAYSCALE]);
		glBufferData(GL_ARRAY_BUFFER, tiles, gps.screentexpos_grayscale, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[GRAYSCALE], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		printGLError();

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[CF]);
		glBufferData(GL_ARRAY_BUFFER, tiles, gps.screentexpos_cf, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[CF], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		printGLError();

		glBindBuffer(GL_ARRAY_BUFFER, vbo_id[CBR]);
		glBufferData(GL_ARRAY_BUFFER, tiles, gps.screentexpos_cbr, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attr_loc[CBR], 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
		printGLError();
	}
	void reshape(pair<int, int> size) {// Parameters: grid units
		int w = MIN(MAX(size.first, MIN_GRID_X), MAX_GRID_X);
		int h = MIN(MAX(size.second, MIN_GRID_Y), MAX_GRID_Y);
		cerr<<"reshape(): to " << w << "x" << h << "\n";

		gps_allocate(w, h);
		screen_underlay_reshape();
		makegrid();

		glMatrixMode( GL_PROJECTION);
		glLoadIdentity();
		gluOrtho2D(0, surface->w, 0, surface->h);
		glMatrixMode( GL_MODELVIEW);
		glLoadIdentity();

		float Pw = surface->w/gps.dimx;
		float Ph = surface->h/gps.dimy;

		if (Pw > Ph) {
			Psz = Pw;
			Parx = 1.0;
			Pary = Ph/Pw;
		} else {
			Psz = Ph;
			Parx = Pw/Ph;
			Pary = 1.0;
		}

		fprintf(stderr, "reshape_gl(): to %dx%d grid %dx%d Ps %0.2f %0.2f %0.2f\n",
				surface->w, surface->h, gps.dimx, gps.dimy, Psz, Parx, Pary);

		glUniform1f(unif_loc[POINTSIZE], Psz);
		glUniform2f(unif_loc[PAR], Parx, Pary);
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
		printGLError();
		if (do_swap)
			SDL_GL_SwapBuffers();
		f_counter ++;
		if ((do_dump_screen > 0) && (f_counter % do_dump_screen == 0))
			dump_screen("screendump");
	}
	virtual void set_fullscreen() 			{ zoom(zoom_fullscreen); }
	//rtual void swap_arrays() 				{ if (0) std::cerr<<"swap_arrays(): do not need.\n"; }

	virtual void zoom(zoom_commands cmd) {

		switch (cmd) {
			case zoom_in:
				Psz ++;
				break;
			case zoom_out:
				Psz --;
				break;
			case zoom_reset:
				// set 1:1 Psz
				Psz = 16;
				break;
			case zoom_resetgrid:
				// dis be called when overriden zoom is released
				Psz = 16;
				break;
			case zoom_fullscreen:
				if (enabler.is_fullscreen()) {
					init.display.desired_windowed_width = surface->w;
					init.display.desired_windowed_height = surface->h;
					resize(init.display.desired_fullscreen_width,
							init.display.desired_fullscreen_height);
				} else {
					resize(init.display.desired_windowed_width, init.display.desired_windowed_height);
				}
				return;
				break;
		}
		resize(surface->w, surface->h);

	}
	virtual void grid_resize(int w, int h) {
		/* dis gets called from enablerst::override_grid_size() only
		 * this means the grid size is fixed. thus do not touch it,
		 * just recalculate Psize/Parx/Pary to maximum window
		 * utilization.
		 */
		std::cerr<<"renderer_glsl::grid_resize(): "<<w<<"x"<<h<<" (t).\n";
		gps_allocate(w, h);
		screen_underlay_reshape();
		makegrid();
		resize(surface->w, surface->h);
	}
	virtual void resize(int w, int h) {
		/* dis gets called on SDL_VIDEORESIZE event
		 * 1. IFF enabler.overridden_grid_sizes.size()
		 * 	 - recompute Psz/Parx/Pary only, do not touch grid
		 * 2. ELSE
		 *   - recompute Parx/Pary and grid size based only on the
		 *   previous psize.(keep it)
		 *   do we keep Paspect ratio??
		 *
		 */
		int grid_w, grid_h;
		if (enabler.overridden_grid_sizes.size()) {
			grid_w = gps.dimx;
			grid_h = gps.dimy;
		} else {
			/* resize grid so as to attempt to keep
			 * tile aspect ratio closer to the one
			 * of the texture, keeping in mind current
			 * zoom level (which is the Psz)
			 */

			float _Parx = tile_w > tile_h ? 1.0 : (float)tile_w/tile_h;
			float _Pary = tile_w < tile_h ? 1.0 : (float)tile_h/tile_w;

			grid_w = MIN( MAX ( (w/(Psz*_Parx)), MIN_GRID_X), MAX_GRID_X);
			grid_h = MIN( MAX ( (h/(Psz*_Pary)), MIN_GRID_Y), MAX_GRID_Y);

			gps_allocate(grid_w, grid_h);
			screen_underlay_reshape();
			makegrid();
		}
		int psz_x = w / gps.dimx;
		int psz_y = h / gps.dimy;
		if (psz_x > psz_y) {
			Psz = psz_x;
			Parx = (float)psz_y/psz_x;
			Pary = 1.0;
		} else {
			Psz = psz_y;
			Parx = 1.0;
			Pary = (float)psz_x/psz_y;
		}
		if (snap_window) {
			w = gps.dimx * psz_x;
			h = gps.dimy * psz_y;
		}
		if ((w != surface->w) || (h != surface->h)) {
			set_mode(w, h);
			opengl_setmode();
		}
		glUniform1f(unif_loc[POINTSIZE], Psz);
		glUniform2f(unif_loc[PAR], Parx, Pary);

		fprintf(stderr, "resize(): to %dx%d grid %dx%d Ps %0.2f %0.2f %0.2f\n",
				surface->w, surface->h, gps.dimx, gps.dimy, Psz, Parx, Pary);
	}
	renderer_glsl() {
		f_counter = 0;snap_window = true;
		texture_filter = init.window.flag.has_flag(INIT_WINDOW_FLAG_TEXTURE_LINEAR) ? GL_LINEAR : GL_NEAREST;
		SDL_EnableKeyRepeat(0, 0); // Disable key repeat
		SDL_WM_SetCaption(GAME_TITLE_STRING, NULL); // Set window title/icon.
		SDL_Surface *icon = IMG_Load("data/art/icon.png");
		if (icon != NULL) {
			SDL_WM_SetIcon(icon, NULL);
			SDL_FreeSurface(icon);
		}
		Psz = 16;
		Parx = 1.0;
		Pary = 1.0;

		if (init.display.desired_fullscreen_width == 0
				|| init.display.desired_fullscreen_height == 0) {
			const struct SDL_VideoInfo *info = SDL_GetVideoInfo();
			init.display.desired_fullscreen_width = info->current_w;
			init.display.desired_fullscreen_height = info->current_h;
		}

		// Initialize our window
		bool worked = set_mode(
				enabler.is_fullscreen() ? init.display.desired_fullscreen_width
						: init.display.desired_windowed_width,
				enabler.is_fullscreen() ? init.display.desired_fullscreen_height
						: init.display.desired_windowed_height );

		// Fallback to windowed mode if fullscreen fails
		if (!worked && enabler.is_fullscreen()) {
			enabler.fullscreen = false;
			report_error("SDL initialization failure, trying windowed mode",
					SDL_GetError());
			worked = set_mode(init.display.desired_windowed_width,
					init.display.desired_windowed_height);
		}
		// Quit if windowed fails
		if (!worked) {
			report_error("SDL initialization failure", SDL_GetError());
			exit(EXIT_FAILURE);
		}
		screen_underlay_reshape();
		opengl_init();
	}
	virtual bool get_mouse_coords(int &x, int &y) {
		int mouse_x, mouse_y;
		SDL_GetMouseState(&mouse_x, &mouse_y);
		x = double(mouse_x) / Psz*Parx * double(gps.dimx);
		y = double(mouse_y) / Psz*Pary * double(gps.dimy);
		return true;
	}
	virtual bool uses_opengl() {
		bool very_true = true;
		return true && very_true;
	}
};
