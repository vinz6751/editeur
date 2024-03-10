#ifndef EDITEUR_H
#define EDITEUR_H

#include <stdio.h>

typedef unsigned short LINENR; /* ID of a line */

typedef struct line_t {
	LINENR prev,next; /* previous and next */
	char *d;
} LINE;


/* Buffers store the data for a file being edited. */

typedef struct buffer_t {
	struct buffer_t *prev;
	struct buffer_t *next;
	
	char filename[FILENAME_MAX];
	long size;
	LINENR lines;	
	char *d; /* data */
} BUFFER;


/* A terminal is the device that the user sees and types on, so it provides the abstraction
 * of the underlying ways to display and get user input.
 */

typedef struct {
	int width;
	int height;
	int ncolors;
} TERM_INFO; /* Info about a terminal */

typedef struct {
	void (*init)(void);
	void (*deinit)(void);
	void (*clear_screen)(void);
	void (*set_cursor_position)(int x, int y);
	void (*cursor_on)(void);
	void (*cursor_off)(void);
	void (*get_key)(char *ascii, int *scancode, int *dead_keys);
	void (*printc)(char);
	void (*print)(const char *); /* For performance */
	void (*get_info)(TERM_INFO*);
} TERM;


/* A window is a view of a file. That's what the user interacts with. */

typedef struct window_t {
	BUFFER *buffer; /* Contains the text data */
	TERM   *term;   /* Terminal */
	int cx,cy;      /* Cursor position (column/row) relative to wx_min/wx_max */
	int t_line,t_col; /* Number of the first line and column to display at the top left of the window */
	int wx_min,wy_min,wx_max,wy_max; /* Window position on the terminal */
	int allocated;
} WINDOW;



#endif
