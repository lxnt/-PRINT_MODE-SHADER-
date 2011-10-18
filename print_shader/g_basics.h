#ifndef G_BASICS_H
#define G_BASICS_H

#define MAX_GRID_X 256
#define MAX_GRID_Y 256
#define MIN_GRID_X 80
#define MIN_GRID_Y 25

#ifndef MAX
# define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
# define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef ARRSZ
# define ARRSZ(arr) (sizeof (arr) / sizeof ((arr)[0]))
#endif
#ifndef CLAMP
#define CLAMP(x,a,b) MIN(MAX((x),(a)),(b))
#endif

// GL error macro
extern int glerrorcount;

#ifdef DEBUG
# define printGLError() do { GLenum err; do { err = glGetError(); if (err && glerrorcount < 40) { printf("GL error: 0x%x in %s:%d\n", err, __FILE__ , __LINE__); glerrorcount++; } } while(err); } while(0);
# define deputs(str) puts(str)
#else
# define printGLError()
# define deputs(str)
#endif

#endif


#define fputsGLError(stream) \
	do { \
		GLenum err = glGetError(); \
		const char *errn; \
		while (err != GL_NO_ERROR) { \
			switch(err) { \
				case GL_INVALID_ENUM: \
					errn = "GL_INVALID_ENUM"; \
					break; \
				case GL_INVALID_VALUE: \
					errn = "GL_INVALID_VALUE"; \
					break; \
				case GL_INVALID_OPERATION: \
					errn = "GL_INVALID_OPERATION"; \
					break; \
				case GL_STACK_OVERFLOW: \
					errn = "GL_STACK_OVERFLOW"; \
					break; \
				case GL_STACK_UNDERFLOW: \
					errn = "GL_STACK_UNDERFLOW"; \
					break; \
				case GL_OUT_OF_MEMORY: \
					errn = "GL_OUT_OF_MEMORY"; \
					break; \
				case GL_TABLE_TOO_LARGE: \
					errn = "GL_TABLE_TOO_LARGE"; \
					break; \
				default: \
					errn = NULL; \
					fprintf(stream, "Unknown GL Error 0x%04x at %s:%d\n", err, __FILE__, __LINE__-1); \
					break; \
			} \
			if (errn) \
				fprintf(stream, "%s at %s:%d\n", errn, __FILE__, __LINE__-1); \
			err = glGetError(); \
		}\
		fflush(stream);\
		if(0) abort();\
	} while(0)
