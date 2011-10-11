// SHADER


extern texdumpst texdumper;
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

class renderer_glsl : public renderer {
	GLuint tex_id[2];
	GLuint vbo_id[7];
	GLint  attr_loc[7];
	GLint  unif_loc[6];
	GLuint shader;
	GLint txsz_w, txsz_h;
	bool reset_grid, do_dump, do_swap;
	GLfloat *grid;
	SDL_Surface *surface;

	enum uniforms {
		FONT,
		ANSI,
		TXSZ,
		FINAL_ALPHA,
		POINTSIZE,
		VIEWPOINT
	};

	enum attrarrays {
		SCREEN,
		TEXPOS,
		ADDCOLOR,
		GRAYSCALE,
		CF,
		CBR,
		POSITION
	};

	uint32_t *screen_underlay;

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
				std::cerr<<"set_mode(): requested single-buffering, failed, don't care because of fullscreen.\n";
			else
				report_error("OpenGL","Requested single-buffering not available");
		}
		opengl_setmode();
		return true;
	}

	void buffer_binding() {
		GLint param;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &param);
		std::cerr << "buffer_binding(): currently bound buffer: " << param << "\n";
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
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		printGLError();
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		printGLError();
		glUniform2f(unif_loc[TXSZ], texdumper.w_t, texdumper.h_t);
		printGLError();
		fprintf(stderr, "accepted font texture (name=%d): %dx%dpx oa\n",
				tex_id[FONT], cats->w, cats->h);
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
		std::ifstream f("data/vertex.shader", ios::binary);
		GLint v_len = f.seekg(0, std::ios::end).tellg();
		f.seekg(0, ios::beg);
		GLchar *v_src = new GLchar[v_len + 1];
		f.read(v_src, v_len);
		f.close();
		v_src[v_len] = 0;
		f.open("data/fragment.shader", ios::binary);
		GLint f_len = f.seekg(0, ios::end).tellg();
		f.seekg(0, ios::beg);
		GLchar *f_src = new GLchar[f_len + 1];
		f.read(f_src, f_len);
		f.close();
		f_src[f_len] = 0;

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
		glEnableVertexAttribArray(attr_loc[TEXPOS]);
		glEnableVertexAttribArray(attr_loc[ADDCOLOR]);
		glEnableVertexAttribArray(attr_loc[GRAYSCALE]);
		glEnableVertexAttribArray(attr_loc[CF]);
		glEnableVertexAttribArray(attr_loc[CBR]);
		glEnableVertexAttribArray(attr_loc[POSITION]);
		printGLError();

		glUniform1i(unif_loc[ANSI], 0); 		// GL_TEXTURE0 : ansi color strip
		glUniform1i(unif_loc[FONT], 1); 		// GL_TEXTURE1 : font
		glUniform1f(unif_loc[FINAL_ALPHA], 1.0);
		glUniform1f(unif_loc[POINTSIZE], 16);
		printGLError();
		/* note: TXSZ and POINTSIZE are not bound yet. */
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
		glGenBuffers(7, vbo_id);
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
		reset_grid = true;
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

		if (reset_grid) {
			makegrid();
			reset_grid = false;
			std::cerr<<"update_vbos(): grid reset.\n";
		}
	}
	void reshape_gl() {
		std::cerr<<"reshape_gl(): to "<<surface->w<<"x"<<surface->h<<".\n";
		reset_grid = true;
		glMatrixMode( GL_PROJECTION);
		glLoadIdentity();
		gluOrtho2D(0, surface->w, 0, surface->h);
		glMatrixMode( GL_MODELVIEW);
		glLoadIdentity();
		GLfloat psize = MIN(surface->w/gps.dimx, surface->h/gps.dimy);
		glUniform1f(unif_loc[POINTSIZE], psize);
	}
	// Parameters: grid units
	void reshape(pair<int, int> size) {
		int w = MIN(MAX(size.first, MIN_GRID_X), MAX_GRID_X);
		int h = MIN(MAX(size.second, MIN_GRID_Y), MAX_GRID_Y);
		cerr<<"reshape(): to " << w << "x" << h << "\n";
		gps_allocate(w, h);
		screen_underlay_reshape();
		reshape_gl();
	}
	void dump_screen(const char *fname) {
		std::ofstream f;
		const int dimx = init.display.grid_x;
		const int dimy = init.display.grid_y;

		f.open(fname, std::ios::app | std::ios::binary);
		f.write((char *)(&dimx), sizeof(dimx));
		f.write((char *)(&dimy), sizeof(dimy));
		f.write((char *)(screen), 4*dimx*dimy);
		f.write((char *)(screentexpos), sizeof(long)*dimx*dimy);
		f.write((char *)(screentexpos_addcolor), dimx*dimy);
		f.write((char *)(screentexpos_grayscale), dimx*dimy);
		f.write((char *)(screentexpos_cf), dimx*dimy);
		f.write((char *)(screentexpos_cbr), dimx*dimy);
		f.close();
		std::cerr<<"Dumped "<<(12*dimx*dimx)<<" bytes, size "<<dimx<<"x"<<dimy<<" tiles\n";
	}
int off_x, off_y, size_x, size_y;
int zoom_steps, forced_steps;
int natural_w, natural_h; // How large our view would be if it wasn't zoomed

	void compute_forced_zoom() {
		forced_steps = 0;
		pair<int,int> zoomed = compute_zoom();
		while (zoomed.first < MIN_GRID_X || zoomed.second < MIN_GRID_Y) {
			forced_steps++;
			zoomed = compute_zoom();
		}
		while (zoomed.first > MAX_GRID_X || zoomed.second > MAX_GRID_Y) {
			forced_steps--;
			zoomed = compute_zoom();
		}
	}

	pair<int,int> compute_zoom(bool clamp = false) {
		const int dispx = enabler.is_fullscreen() ?
			init.font.large_font_dispx :
			init.font.small_font_dispx;
		const int dispy = enabler.is_fullscreen() ?
			init.font.large_font_dispy :
			init.font.small_font_dispy;
		int w, h;
		if (dispx < dispy) {
			w = natural_w + zoom_steps + forced_steps;
			h = double(natural_h) * (double(w) / double(natural_w));
		} else {
			h = natural_h + zoom_steps + forced_steps;
			w = double(natural_w) * (double(h) / double(natural_h));
		}
		if (clamp) {
			w = MIN(MAX(w, MIN_GRID_X), MAX_GRID_X);
			h = MIN(MAX(h, MIN_GRID_Y), MAX_GRID_Y);
		}
		return make_pair(w,h);
	}

	int f_counter;


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
		if ((f_counter++ % 5122) and false)
			std::cerr<<"5122nd frame rendered.\n";
		if (do_dump)
			dump_screen("sd-fail");

	}
	virtual void set_fullscreen() 			{ zoom(zoom_fullscreen); }
	//rtual void swap_arrays() 				{ if (0) std::cerr<<"swap_arrays(): do not need.\n"; }

	virtual void zoom(zoom_commands cmd) {
		pair<int, int> before = compute_zoom(true);
		int before_steps = zoom_steps;
		switch (cmd) {
			case zoom_in:
				zoom_steps -= init.input.zoom_speed;
				break;
			case zoom_out:
				zoom_steps += init.input.zoom_speed;
				break;
			case zoom_reset:
				zoom_steps = 0;
			case zoom_resetgrid:
				compute_forced_zoom();
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
		pair<int, int> after = compute_zoom(true);
		if (after == before && (cmd == zoom_in || cmd == zoom_out))
			zoom_steps = before_steps;
		else
			reshape(after);
	}
  // Parameters: grid size
	virtual void grid_resize(int w, int h) {
		std::cerr<<"renderer_glsl::grid_resize(): "<<w<<"x"<<h<<" (t).\n";
		reshape(make_pair(w, h));
	}
	virtual void resize(int w, int h) {
		std::cerr<<"renderer_glsl::resize(): "<<w<<"x"<<h<<" (px).\n";
		// (Re)calculate grid-size
		int dispx = enabler.is_fullscreen() ? init.font.large_font_dispx
				: init.font.small_font_dispx;
		int dispy = enabler.is_fullscreen() ? init.font.large_font_dispy
				: init.font.small_font_dispy;
		natural_w = MAX(w / dispx, 1);
		natural_h = MAX(h / dispy, 1);
		// Compute forced_steps so we satisfy our grid-size limits
		compute_forced_zoom();
		// Force a full display cycle
		gps.force_full_display_count = 1;
		enabler.flag |= ENABLERFLAG_RENDER;
		// Reinitialize the video <- no fucking need to on proper system.
		//opengl_fini();
		set_mode(w, h);
		opengl_setmode();
		// Only reshape if we're free to pick grid size
		if (enabler.overridden_grid_sizes.size() == 0)
			reshape(compute_zoom());
	}
	renderer_glsl() {
		zoom_steps = forced_steps = f_counter = 0;
		do_dump = false;

		SDL_EnableKeyRepeat(0, 0); // Disable key repeat
		SDL_WM_SetCaption(GAME_TITLE_STRING, NULL); // Set window title/icon.
		SDL_Surface *icon = IMG_Load("data/art/icon.png");
		if (icon != NULL) {
			SDL_WM_SetIcon(icon, NULL);
			SDL_FreeSurface(icon);
		}

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
		// Initialize opengl
		opengl_init();

	}

	virtual bool get_mouse_coords(int &x, int &y) {
		int mouse_x, mouse_y;
		SDL_GetMouseState(&mouse_x, &mouse_y);
		mouse_x -= off_x;
		mouse_y -= off_y;
		if (mouse_x < 0 || mouse_y < 0 || mouse_x >= size_x || mouse_y
				>= size_y)
			return false; // Out of bounds
		x = double(mouse_x) / double(size_x) * double(gps.dimx);
		y = double(mouse_y) / double(size_y) * double(gps.dimy);
		return true;
	}
	virtual bool uses_opengl() {
		bool very_true = true;
		return true && very_true;
	}

};
