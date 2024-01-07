#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOS 0


#define EOK 0

/* LINE MANAGEMENT *************************************************************/
/* Lines are stored independently of one another but are linked using a double-linked list.
 * The storage of lines is global to the program / shared between all buffers.
 * The list of free lines is also managed as a linked-list.
 */
 

#define MAX_LINES 32766
#define INVALID_LINE 32767

typedef unsigned short LINENR;

typedef struct line_t {
	LINENR prev,next; /* previous and next */
	char *d;
} LINE;

void line_init(void);

LINENR free_lines; /* First free line of the chain */
LINE storage[MAX_LINES];

void line_init(void) {
	LINENR i;
	free_lines = 0;

	for (i=0 ; i<MAX_LINES; i++) {
		storage[i].prev = i - 1;
		storage[i].next = i + 1;
	}
	storage[0].prev = storage[MAX_LINES-1].next = INVALID_LINE;
}

void line_link(LINENR linenr, LINENR prev, LINENR next) {
	LINE *line = &storage[linenr];

	line->prev = prev;
	line->next = next;
 	
	if (prev != INVALID_LINE)
		storage[prev].next = linenr;
	if (next != INVALID_LINE)
		storage[next].prev = linenr;
}

void line_unlink(LINENR linenr) {
	LINE *line = &storage[linenr];
	
	if (line->prev != INVALID_LINE)
		storage[line->prev].next = line->next;
	if (line->next != INVALID_LINE)
		storage[line->next].prev = line->prev;
	/* Return the line to the storage */
	line->next = free_lines;
	line->prev = INVALID_LINE; /* Not really useful, free lines could be simple-chained */
	free_lines = linenr;
}

LINENR line_alloc(LINENR prev, LINENR next, char *content) {
	LINENR linenr;
	char *lcontent;
	
	if (free_lines == INVALID_LINE) {
		return INVALID_LINE;
	}
	/* We try to allocate memory early as this is what could fail */
	lcontent = malloc(strlen(content)+1/*EOL*/);
	if (lcontent == NULL)
		return INVALID_LINE;
	
	linenr = free_lines;
	/* Update free lines list */
	free_lines = storage[free_lines].next;
	storage[free_lines].prev = INVALID_LINE;
	/* Link allocated line */
	line_link(linenr, prev, next);
	/* Copy content */
	strcpy(lcontent, content); 
	storage[linenr].d = lcontent;

	return linenr;
}

void line_free(LINENR linenr) {
	LINE *line = &storage[linenr];
	
	if (line->d)
		free(line->d);
	line_unlink(linenr);
}

LINE *line_get(LINENR linenr) {
	assert(linenr >= 0);
	assert(linenr < MAX_LINES);
	return &storage[linenr];
}

/* BUFFER MANAGEMENT *********************************************************/
/* Buffers store the data for a file being edited.
 */
typedef struct buffer_t {
	struct buffer_t *prev;
	struct buffer_t *next;
	
	char filename[FILENAME_MAX];
	long size;
	LINENR lines;
	
	char *d; /* data */
} BUFFER;


BUFFER *buffers = NULL;

BUFFER *buffer_create() {
	BUFFER *buf;

	buf = malloc(sizeof(BUFFER));
	if (!buf) {
		return NULL;
	}

	buf->next = buffers;
	buffers = buf;
	return buf;
}

void buffer_destroy(BUFFER *buffer) {
	if (buffer->prev)
		buffer->prev->next = buffer->next;
	if (buffer->next)
		buffer->next->prev = buffer->prev;

	if (buffer->d)
		free(buffer->d);
	free(buffer);
}

int buffer_load(BUFFER *buffer, const char *filename) {
	FILE *f = NULL;
	int ret;
	char *line;
	LINENR linenr;

	f = fopen(filename,"r");
	if (!f) {
		ret = errno;
		goto quit;
	}

	if (fseek(f, 0, SEEK_END)) {
		ret = errno;
		goto quit;
	}
	buffer->size = ftell(f);
	rewind(f);
	
	buffer->d = (char*)malloc(buffer->size+1/*We add NULL for safety*/);
	if (buffer->d == NULL) {
		ret = ENOMEM;
		goto quit;
	}
	buffer->d[buffer->size] = '\0';
	
	if (fread(buffer->d, 1, buffer->size, f) != buffer->size) {
		ret = errno;
		free(buffer->d);
		goto quit;
	}

	fclose(f);
	f = NULL;

	/* Read through the buffer to construct lines */
    line = buffer->d;
	linenr = INVALID_LINE;
	while (*line) {
		/* Find end of line */
		char *eol = line;
		int   next;
		while (*eol && *eol != '\n') {
			eol++;
		}
		next = (*eol) ? 1 : 0; /* Don't advance if we're at the end of the file */
		if (*eol)
			*eol = '\0';

		linenr = line_alloc(linenr, INVALID_LINE, line);
		if (buffer->lines == INVALID_LINE)
			buffer->lines = linenr; /* Only store beginning of the chain */
		
		line = eol + next;
	}

	ret = EOK;
	
 quit:
	if (f)
		fclose(f);

	return ret;
}


LINENR buffer_find_line_by_number(const BUFFER *buffer, int row) {
	LINENR bufline;

	--row; /* Rows start from 1 but we count from 0 */
	assert(row >= 0);
	for (bufline = buffer->lines, --row; bufline != INVALID_LINE && row >= 0; bufline = storage[bufline].next, row--)
		;
	return bufline;
	
}

void buffer_dump_lines(const BUFFER *buffer) {
	LINENR linenr;
	int i;

	for (linenr = buffer->lines, i=1; linenr != INVALID_LINE; linenr = storage[linenr].next, i++) {
		printf("%05d:%05d: %s\n", i, linenr, storage[linenr].d);
	}
}


/* Terminal ************************************************************************/
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
	void (*get_key)(char *ascii, int *scancode);
	void (*printc)(char);
	void (*print)(const char *); /* For performance */
	void (*get_info)(TERM_INFO*);
} TERM;

#if TOS
#include <osbind.h>
void vt52_output(const char *s) { Cconws(s); } /* Cconws will interpret VT-52 codes */
void vt52_printc(char c) { Crawio((unsigned char)c); } /* Crawio doesn't interpret VT-52 codes. */
void vt52_init(void) { vt52_clear_screen(); }
void vt52_deinit(void) { }
void vt52_clear_screen(void) { vt52_output("\033E"); }
void vt52_cursor_on(void) { vt52_output("\033e"); }
void vt52_cursor_off(void) { vt52_output("\033f"); }
static char vt52curposbuf="\033Yyx";
void vt52_set_cursor_position(int x, int y) {
	vt52curposbuf[2] = (char)(y + 32);
	vt52curposbuf[3] = (char)(x + 32);
	vt52_output(vt52curposbuf);
}
void vt52_get_key(char *ascii, int *scancode, int *dead_keys) {
	long ret = Crawio(0xff);
	ascii = ret & 0xff;
	scancode = (ret & 0xff0000) >> 16;
	dead_keys = (ret & 0xff000000) >> 24;
}
void get_info(TERM_INFO *terminfo) {
	width = 80; /* TODO */
	height = 25;
	ncolors = 1;
}
TERM vt52 = {
	vt52_init,
	vt52_deinit,
	vt52_clear_screen,
	vt52_set_cursor_position,
	vt52_get_key,
	vt52_printc,
	vt52_output /* CAVEAT: this interprets VT-52 escape sequences */
};
#endif


/* Window management ***************************************************************/
/* A window is a view of a file. That's what the user interacts with.
 */

typedef struct window_t {
	BUFFER *buffer; /* Mandatory */
	TERM   *term;   /* Terminal */
	int cx,cy;      /* Cursor position (column/row) */
	int t_line,t_col; /* Number of the firs tline and column to display at the top left of the window */
	int wx_min,wy_min,wx_max,wy_max; /* Window position on the terminal */
	int allocated;
} WINDOW;

WINDOW window[8];

WINDOW *window_create(BUFFER *buffer) {
	WINDOW *win;

	win = &window[0];
	win->buffer = buffer;
	win->cx = win->cy = win->t_line = win->t_col = 1;
#if TOS
	win->term = vt52;
#endif
	/* Assume full screen for now */

	{
		TERM_INFO ti;
		win->term->get_info(&ti);
		win->wx_min = 1;
		win->wx_max = ti.width-1;
		win->wy_min = 1;
		win->wy_max = ti.height-1;
	}

	return win;
}

void window_clear(WINDOW *win) {
	int c,r;

	/* TODO: Speed this up */
	window_clear(win);
	for (r=win->wy_min; r<=win->wy_max; r++) {
		win->term->set_cursor_position(win->wx_min, r);
		for (c=win->wx_min; c<=win->wx_max; c++) {
			win->term->printc(' ');
		}
	}
}

void window_paint(WINDOW *win) {
	int wr; /* Window row */
	int tr; /* Text row */
	int width = win->wy_max - win->wy_min + 1;

	int linelen;

	win->term->cursor_off();
	window_clear(win);
   
	for (wr=win->wy_min, tr= win->t_line; wr<=win->wy_max; tr++) {
		char *l = line_get(buffer_find_line_by_number(win->buffer,tr))->d;
		win->term->set_cursor_position(win->wx_min,wr);
		linelen = strlen(l) - win->t_col; /* Number of chars to actually print on the row */
		if (linelen <= 0)
			continue;
		if (linelen > width)
			linelen = width;
		l = &l[win->t_col];
		while (linelen--)
			win->term->printc(*l++);
	}
	
	win->term->cursor_on(); /* Si la fenêtre est active */
}


/* MAIN ****************************************************************************/ 

int main(void) {
	line_init();

	BUFFER *buffer = buffer_create();
	if (!buffer)
		return ENOMEM;
	printf("Loading buffer\n");
	buffer_load(buffer, "editeur.c");

	/* Delete line */
	line_free(storage[buffer->lines].next);

	/* Insert line */
	printf("line 0:%s\n", line_get(buffer_find_line_by_number(buffer,1))->d);
	   
	return EOK;
}
