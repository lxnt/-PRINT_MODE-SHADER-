#include "platform.h"
#include <string.h>
#include <math.h>
#include <iosfwd>
#include <iostream>
#include <ios>
#include <streambuf>
#include <istream>
#include <ostream>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <fstream>
#include <zlib.h>
#include <cassert>
#include <cstdio>

#include "svector.h"
#include "ttf_manager.hpp"

#ifdef WIN32

/*
#ifndef INTEGER_TYPES
	#define INTEGER_TYPES
	typedef short int16_t;
	typedef int int32_t;
	typedef long long int64_t;
	typedef unsigned short uint16_t;
	typedef unsigned int uint32_t;
	typedef unsigned long long uint64_t;
#endif
 */

typedef int32_t VIndex;
typedef int32_t Ordinal;

#endif

#include "random.h"

#include "endian.h"

#include "files.h"

#include "enabler.h"

#include "textlines.h"

#include "find_files.h"

#include "basics.h"

#include "g_basics.h"

#include "texture_handler.h"

#include "graphics.h"

#include "music_and_sound_g.h"

#include "init.h"

#include "interface.h"

#include "curses.h"

using namespace std;


#pragma comment( lib, "opengl32.lib" )			// Search For OpenGL32.lib While Linking
#pragma comment( lib, "glu32.lib" )				// Search For GLu32.lib While Linking

extern enablerst enabler;
extern texture_handlerst texture;
graphicst gps;
extern interfacest gview;

extern string errorlog_prefix;

void process_object_lines(textlinesst &lines,string &chktype,string &graphics_dir);

// Add, then increment to the (possible) PBO alignment requirement
static void align(size_t &sz, off_t inc) {
	sz += inc;
	while (sz%64) sz++; // So.. tired.. FIXME.
}

void graphicst::resize(int x, int y)  {
	dimx = x; dimy = y;
	init.display.grid_x = x;
	init.display.grid_y = y;
	setclipping(0, x-1, 0, y-1);
	force_full_display_count++;
	screen_limit = screen + dimx * dimy * 4;
}

void graphicst::addcoloredst(const char *str,const char *colorstr)
{
#ifdef ORIGST
	const int slen = strlen(str);
	int s;
	for(s=0; s < slen && screenx < init.display.grid_x; s++)
	{
		if(screenx<0)
		{
			s-=screenx;
			screenx=0;
			if (s >= slen) break;
		}

		changecolor((colorstr[s] & 7),((colorstr[s] & 56))>>3,((colorstr[s] & 64))>>6);
		addchar(str[s]);
	}
#else
	newg.addcoloredst(str, colorstr);
#endif
}

void graphicst::addst(const string &str, justification just)
{
#ifdef ORIGST
	if (just == justify_cont)
		just = justify_left;
	if (just != not_truetype && ttf_manager.was_init()) {
		struct ttf_id id = {str, screenf, screenb, screenbright, just};
		pair<int,int> handleAndWidth = ttf_manager.get_handle(id);
		const int handle = handleAndWidth.first;
		const int width = handleAndWidth.second;
		int ourx;
		// cout << str.size() << " " << width << endl;
		switch (just) {
		case justify_center:
			ourx = screenx + (str.size() - width) / 2;
			break;
		case justify_right:
			ourx = screenx + (str.size() - width);
			break;
		default:
			ourx = screenx;
			break;
		}
		unsigned char * const s = screen + ourx*dimy*4 + screeny*4;
		s[0] = (handle >> 16) & 0xff;
		s[1] = (handle >> 8) & 0xff;
		s[2] = handle & 0xff;
		s[3] = GRAPHICSTYPE_TTF;
		// Also set the other tiles this text covers
		for (int x = 1; x < width; ++x) {
			*(s + x*dimy*4 + 0) = (handle >> 16) & 0xff;
			*(s + x*dimy*4 + 1) = (handle >> 8) & 0xff;
			*(s + x*dimy*4 + 2) = handle & 0xff;
			*(s + x*dimy*4 + 3) = GRAPHICSTYPE_TTFCONT;
		}
		screenx = ourx + width;
	} else {
		int s;
		for(s=0;s<str.length()&&screenx<init.display.grid_x;s++)
		{
			if(screenx<0)
			{
				s-=screenx;
				screenx=0;
				if(s>=str.length())break;
			}

			addchar(str[s]);
		}
	}
#else
	newg.addst(str, just);
#endif
}
void graphicst::addst(const char *str, justification just)
{
#ifdef ORIGST
	string s(str);
	addst(s, just);
#else
	newg.addst(str, just);
#endif
}
void graphicst::erasescreen_clip()
{
#ifdef ORIGST
	changecolor(0,0,0);
	short x2,y2;
	for(x2=clipx[0];x2<=clipx[1];x2++)
	{
		for(y2=clipy[0];y2<=clipy[1];y2++)
		{
			locate(y2,x2);
			addchar(' ');
		}
	}
#else
	newg.erasescreen_clip();
#endif
}

void graphicst::erasescreen_rect(int x1, int x2, int y1, int y2)
{ 
#ifdef ORIGST
	changecolor(0,0,0);
	for (int x = x1; x <= x2; x++) {
		for (int y = y1; y <= y2; y++) {
			locate(y, x);
			addchar(' ');
		}
	}
#else
	newg.erasescreen_rect(x1,x2,y1,y2);
#endif
}

void graphicst::erasescreen()
{
#ifdef ORIGST
	memset(screen, 0, dimx*dimy*4);

	memset(screentexpos, 0, dimx*dimy*sizeof(long));
#else
	newg.erasescreen();
#endif
}

void graphicst::setclipping(long x1,long x2,long y1,long y2)
{
#ifdef ORIGST
	if(x1<0)x1=0;
	if(x2>init.display.grid_x-1)x2=init.display.grid_x-1;
	if(y1<0)y1=0;
	if(y2>init.display.grid_y-1)y2=init.display.grid_y-1;

	clipx[0]=x1;
	clipx[1]=x2;
	clipy[0]=y1;
	clipy[1]=y2;
#else
	newg.setclipping(x1,x2,y1,y2);
#endif
}
void graphicst::dim_colors(long x,long y,char dim)
{
#ifdef ORIGST
	if(x>=clipx[0]&&x<=clipx[1]&&
			y>=clipy[0]&&y<=clipy[1])
	{
		switch(dim)
		{
		case 4:
			switch(screen[x*dimy*4 + y*4 + 2])
			{
			case 4:
			case 5:
			case 6:
				screen[x*dimy*4 + y*4 + 2]=1;
				break;
			case 2:
			case 7:
				screen[x*dimy*4 + y*4 + 2]=3;
				break;
			}
			switch(screen[x*dimy*4 + y*4 + 1])
			{
			case 4:
			case 5:
			case 6:
				screen[x*dimy*4 + y*4 + 1]=1;
				break;
			case 2:
			case 7:
				screen[x*dimy*4 + y*4 + 1]=3;
				break;
			}
			if(screen[x*dimy*4 + y*4 + 1]==screen[x*dimy*4 + y*4 + 2])screen[x*dimy*4 + y*4 + 1]=0;
			screen[x*dimy*4 + y*4 + 3]=0;
			if(screen[x*dimy*4 + y*4 + 1]==0&&screen[x*dimy*4 + y*4 + 2]==0&&screen[x*dimy*4 + y*4 + 3]==0)screen[x*dimy*4 + y*4 + 3]=1;
			break;
			case 3:
				switch(screen[x*dimy*4 + y*4 + 2])
				{
				case 4:
				case 5:
					screen[x*dimy*4 + y*4 + 2]=6;
					break;
				case 2:
				case 7:
					screen[x*dimy*4 + y*4 + 2]=3;
					break;
				}
				switch(screen[x*dimy*4 + y*4 + 1])
				{
				case 1:
					screen[x*dimy*4 + y*4 + 3]=0;
					break;
				case 4:
				case 5:
					screen[x*dimy*4 + y*4 + 1]=6;
					break;
				case 2:
					screen[x*dimy*4 + y*4 + 1]=3;
					break;
				case 7:
					screen[x*dimy*4 + y*4 + 1]=3;
					break;
				}
				if(screen[x*dimy*4 + y*4 + 1]!=7)screen[x*dimy*4 + y*4 + 3]=0;
				if(screen[x*dimy*4 + y*4 + 1]==screen[x*dimy*4 + y*4 + 2]&&
						screen[x*dimy*4 + y*4 + 3]==0)screen[x*dimy*4 + y*4 + 1]=0;
				if(screen[x*dimy*4 + y*4 + 1]==0&&screen[x*dimy*4 + y*4 + 2]==0&&screen[x*dimy*4 + y*4 + 3]==0)screen[x*dimy*4 + y*4 + 3]=1;
				break;
				case 2:
					switch(screen[x*dimy*4 + y*4 + 2])
					{
					case 4:
					case 5:
						screen[x*dimy*4 + y*4 + 2]=6;
						break;
					}
					switch(screen[x*dimy*4 + y*4 + 1])
					{
					case 4:
					case 5:
						screen[x*dimy*4 + y*4 + 1]=6;
						break;
					}
					if(screen[x*dimy*4 + y*4 + 1]!=7)screen[x*dimy*4 + y*4 + 3]=0;
					if(screen[x*dimy*4 + y*4 + 1]==screen[x*dimy*4 + y*4 + 2]&&
							screen[x*dimy*4 + y*4 + 3]==0)screen[x*dimy*4 + y*4 + 1]=0;
					if(screen[x*dimy*4 + y*4 + 1]==0&&screen[x*dimy*4 + y*4 + 2]==0&&screen[x*dimy*4 + y*4 + 3]==0)screen[x*dimy*4 + y*4 + 3]=1;
					break;
					case 1:
						if(screen[x*dimy*4 + y*4 + 1]!=7)screen[x*dimy*4 + y*4 + 3]=0;
						if(screen[x*dimy*4 + y*4 + 1]==screen[x*dimy*4 + y*4 + 2]&&
								screen[x*dimy*4 + y*4 + 3]==0)screen[x*dimy*4 + y*4 + 1]=0;
						if(screen[x*dimy*4 + y*4 + 1]==0&&screen[x*dimy*4 + y*4 + 2]==0&&screen[x*dimy*4 + y*4 + 3]==0)screen[x*dimy*4 + y*4 + 3]=1;
						break;
		}
	}
#else
	newg.dim_colors(x,y,dim);
#endif
}
void graphicst::rain_color_square(long x,long y)
{
#ifdef ORIGST
	if(x>=clipx[0]&&x<=clipx[1]&&
			y>=clipy[0]&&y<=clipy[1])
	{
		screen[x*dimy*4 + y*4 + 1]=1;
		screen[x*dimy*4 + y*4 + 2]=0;
		screen[x*dimy*4 + y*4 + 3]=1;
	}
#else
	newg.rain_color_square(x,y);
#endif
}
void graphicst::snow_color_square(long x,long y)
{
#ifdef ORIGST
	if(x>=clipx[0]&&x<=clipx[1]&&
			y>=clipy[0]&&y<=clipy[1])
	{
		screen[x*dimy*4 + y*4 + 1]=7;
		screen[x*dimy*4 + y*4 + 2]=0;
		screen[x*dimy*4 + y*4 + 3]=1;
	}
#else
	newg.snow_color_square(x,y);
#endif
}
void graphicst::color_square(long x,long y,unsigned char f,unsigned char b,unsigned char br)
{
#ifdef ORIGST
	if(x>=clipx[0]&&x<=clipx[1]&&
			y>=clipy[0]&&y<=clipy[1])
	{
		screen[x*dimy*4 + y*4 + 1]=f;
		screen[x*dimy*4 + y*4 + 2]=b;
		screen[x*dimy*4 + y*4 + 3]=br;
	}
#else
	newg.color_square(x,y,f,b,br);
#endif
}
void graphicst::prepare_graphics(string &src_dir)
{
	if(!init.display.flag.has_flag(INIT_DISPLAY_FLAG_USE_GRAPHICS))return;

	texture.clean();

	//GET READY TO LOAD
	svector<char *> processfilename;
	long f;
	textlinesst setuplines;
	char str[400];

	//LOAD THE OBJECT FILES UP INTO MEMORY
	//MUST INSURE THAT THEY ARE LOADED IN THE PROPER ORDER, IN CASE THEY REFER TO EACH OTHER
	string chk=src_dir;
	chk+="graphics/graphics_*";
#ifdef WIN32
	chk+=".*";
#endif
	find_files_by_pattern_with_exception(chk.c_str(),processfilename,"text");

	string chktype="GRAPHICS";
	for(f=0;f<processfilename.size();f++)
	{
		strcpy(str,src_dir.c_str());
		strcat(str,"graphics/");
		strcat(str,processfilename[f]);
		setuplines.load_raw_to_lines(str);

		errorlog_prefix="*** Error(s) found in the file \"";
		errorlog_prefix+=str;
		errorlog_prefix+='\"';
		process_object_lines(setuplines,chktype,src_dir);
		errorlog_prefix.clear();


		delete[] processfilename[f];
	}
	processfilename.clear();

	enabler.reset_textures();
}
void graphicst::add_tile(long texp,char addcolor)
{
#ifdef ORIGST
	if(screenx>=clipx[0]&&screenx<=clipx[1]&&
			screeny>=clipy[0]&&screeny<=clipy[1])
	{
		screentexpos[screenx*dimy + screeny]=texp;
		screentexpos_addcolor[screenx*dimy + screeny]=addcolor;
		screentexpos_grayscale[screenx*dimy + screeny]=0;
	}
#else
	newg.add_tile(texp,addcolor);
#endif
}
void graphicst::add_tile_grayscale(long texp,char cf,char cbr)
{
#ifdef ORIGST
	if(screenx>=clipx[0]&&screenx<=clipx[1]&&
			screeny>=clipy[0]&&screeny<=clipy[1])
	{
		screentexpos[screenx*dimy + screeny]=texp;
		screentexpos_addcolor[screenx*dimy + screeny]=0;
		screentexpos_grayscale[screenx*dimy + screeny]=1;
		screentexpos_cf[screenx*dimy + screeny]=cf;
		screentexpos_cbr[screenx*dimy + screeny]=cbr;
	}
#else
	newg.add_tile_grayscale(texp, cf, cbr);
#endif
}
void graphicst::draw_border(int x1, int x2, int y1, int y2) {
#ifdef ORIGST
	// Upper and lower
	for (int x = x1; x <= x2; x++) {
		locate(y1, x);
		addchar(' ');
		locate(y2, x);
		addchar(' ');
	}
	// Left and right
	for (int y = y1+1; y < y2; y++) {
		locate(y, x1);
		addchar(' ');
		locate(y, x2);
		addchar(' ');
	}
#else
	newg.draw_border(x1,x2,y1,y2);
#endif
}
void graphicst::get_mouse_text_coords(int32_t &mx, int32_t &my) {
	mx = mouse_x; my = mouse_y;
}
void render_things()
{
	//GRAB CURRENT SCREEN AT THE END OF THE LIST
	viewscreenst *currentscreen=&gview.view;
	while(currentscreen->child!=NULL)currentscreen=currentscreen->child;

	//NO INTERFACE LEFT, LEAVE
	if(currentscreen==&gview.view)return;

	if(currentscreen->breakdownlevel==INTERFACE_BREAKDOWN_NONE)
	{
		currentscreen->render();
	}
	else gps.erasescreen();

	// Render REC when recording macros. Definitely want this screen-specific. Or do we?
	const Time now = SDL_GetTicks();
	if (enabler.is_recording() && now % 1000 > 500) {
		gps.locate(0, 20);
		gps.changecolor(4,1,1);
		gps.addst("REC");
	}
	// Render PLAY when playing a macro
	if (enabler.is_macro_playing() && now % 1000 <= 500) {
		gps.locate(0,20);
		gps.changecolor(2,1,1);
		gps.addst("PLAY");
	}
	if (gps.display_frames) {
		ostringstream fps_stream;
		fps_stream << "FPS: " << setw(3) << enabler.calculate_fps() << " (" << enabler.calculate_gfps() << ")";
		string fps = fps_stream.str();
		gps.changecolor(7,3,1);
		static gps_locator fps_locator(0, 25);
		fps_locator(fps.size());
		gps.addst(fps);
	}
}
static void fprintjust(FILE *straem, justification just) {
	switch (just) {
	case justify_left:
		fputs("justify_left)\n", straem);
		return;
	case justify_right:
		fputs("justify_right)\n", straem);
		return;
	case justify_center:
		fputs("justify_center)\n", straem);
		return;
	case justify_cont:
		fputs("justify_cont)\n", straem);
		return;
	case not_truetype:
		fputs("not_truetype)\n", straem);
		return;
	}
}
void newgst::changecolor(short f,short b,char bright) {
	fprintf(stderr, "changecolor(short f=%hd, short b=%hd, char bright=%hhd)\n", f, b, bright );
}
void newgst::addchar(unsigned char c,char advance) {
	fprintf(stderr, "addchar(c=%hhu, char advance=%hhd)\n",c, advance );
}
void newgst::addchar(unsigned int x, unsigned int y, unsigned char c,
			unsigned char f, unsigned char b, unsigned char bright) {
	fprintf(stderr, "addchar(x=%u, y=%u, c=%hhu, f=%hhu, b=%hhu, bright=%hhu)\n", x,y,c,f,b,bright );
}
void newgst::addcoloredst(const char *str,const char *colorstr) {
	fprintf(stderr, "addcoloredst(const char *str='%s', const char *colorstr='%p')\n", str, colorstr);
}
void newgst::addst(const string &str, justification just) {
	fprintf(stderr, "addst(const string &str='%s', justification just=", str.c_str());
	fprintjust(stderr, just);
}
void newgst::addst(const char *str, justification just) {
	fprintf(stderr, "addst(const char *str='%s', justification just=", str );
	fprintjust(stderr, just);
}
void newgst::erasescreen_clip() {
	fprintf(stderr, "erasescreen_clip()\n");
}
void newgst::erasescreen() {
	fprintf(stderr, "erasescreen()\n");
}
void newgst::erasescreen_rect(int x1, int x2, int y1, int y2) {
	fprintf(stderr, "erasescreen_rect(int x1=%d, int x2=%d, int y1=%d, int y2=%d)\n", x1, x2, y1, y2 );
}
void newgst::setclipping(long x1,long x2,long y1,long y2) {
	fprintf(stderr, "setclipping(long x1=%ld, long x2=%ld, long y1=%ld, long y2=%ld)\n", x1,x2,y1,y2 );
}
void newgst::add_tile(long texp,char addcolor) {
	fprintf(stderr, "add_tile(long texp=%ld, char addcolor=%hhd)\n",texp, addcolor );
}
void newgst::add_tile_grayscale(long texp,char cf,char cbr) {
	fprintf(stderr, "add_tile_grayscale(long texp=%ld, char cf=%hhd, char cbr=%hhd)\n", texp, cf, cbr);
}
void newgst::prepare_graphics(string &src_dir) {
	fprintf(stderr, "prepare_graphics(string &src_dir='%s')\n", src_dir.c_str());
}
void newgst::gray_out_rect(long sx,long ex,long sy,long ey) {
	fprintf(stderr, "gray_out_rect(long sx=%ld, long ex=%ld, long sy=%ld, long ey=%ld)\n", sx,sy,ex,ey);
}
void newgst::dim_colors(long x,long y,char dim) {
	fprintf(stderr, "dim_colors(long x=%ld,long y=%ld, char dim=%hhd)\n",x,y, dim );
}
void newgst::rain_color_square(long x,long y) {
	fprintf(stderr, "rain_color_square(long x=%ld,long y=%ld)\n",x,y );
}
void newgst::snow_color_square(long x,long y) {
	fprintf(stderr, "snow_color_square(long x=%ld,long y=%ld)\n",x,y );
}
void newgst::color_square(long x,long y,unsigned char f,unsigned char b,unsigned char br) {
	fprintf(stderr, "color_square(long x=%ld,long y=%ld,unsigned char f=%hhd,unsigned char b=%hhd,unsigned char br=%hhd)\n",
			x,y,f,b,br );
}
void newgst::draw_border(int x1, int x2, int y1, int y2) {
	fprintf(stderr, "draw_border(int x1=%d, int x2=%d, int y1=%d, int y2=%d)\n", x1, x2, y1, y2 );
}
newgst newg;
