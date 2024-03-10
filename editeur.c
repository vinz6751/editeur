#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editeur.h"
#include "vt52.h"

// Pour débug
#include <osbind.h>

#define EOK 0
//#define assert(x) 

/* LINE MANAGEMENT *************************************************************/
/* Lines are stored independently of one another but are linked using a double-linked list.
 * The storage of lines is global to the program / shared between all buffers.
 * The list of free lines is also managed as a linked-list.
 */
 
#define MAX_LINE_LENGTH 256 /* There is no particular reason for this number */
#define MAX_LINES 256
#define INVALID_LINE 32767


void line_init(void);

LINENR free_lines; /* First free line of the chain */
LINE storage[MAX_LINES];

void line_init(void) {
	LINENR i;
	free_lines = 0;

	for (i=0 ; i<MAX_LINES; i++) {
		storage[i].prev = i - 1;
		storage[i].next = i + 1;
		storage[i].d = NULL;
	}
	/* Fix first and last */
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

LINE *line_next(const LINE *line) {
	register LINE l;
	if (line->next == INVALID_LINE)
		return 0L;
	else
		return &storage[line->next];
}

LINE *line_prev(const LINE *line) {
	register LINE l;
	if (line->prev == INVALID_LINE)
		return 0L;
	else
		return &storage[line->prev];
}

int line_set_text(LINE* line, const char *content) {
	char *lcontent;
	int length;

	length = strlen(content);
	lcontent = (char*)malloc(length+1/*EOL*/);
	if (lcontent == NULL) {
		fprintf(stderr,"Not enough memory to allocate line :(\n");
		return ENOMEM; /*Failure*/
	}
	strcpy(lcontent, content);
	lcontent[length] = '\0';
	if (line->d)
		free(line->d);
	line->d = lcontent;
	//	printf("  line %p->data:%s  \n",line->d, line->d ? line->d : "NULL");
	return EOK;
}

LINENR line_alloc(LINENR prev, LINENR next, const char *content) {
	LINENR linenr;
	char *lcontent;
	
	if (free_lines == INVALID_LINE) {
		return INVALID_LINE;
	}
	
	linenr = free_lines;
	/* Update free lines list */
	free_lines = storage[free_lines].next;
	storage[free_lines].prev = INVALID_LINE;
	/* Link allocated line */
	line_link(linenr, prev, next);
	/* Set text, free line and return error if no memory */
	if (line_set_text(&storage[linenr], content) != EOK) {
		line_unlink(linenr);
		return INVALID_LINE;
	}

	return linenr;
}
/* Convenience */
LINENR line_insert_after(LINENR prev, const char *content) {
	return line_alloc(prev, storage[prev].next, content);
}
LINENR line_insert_before(LINENR next, const char *content) {
	return line_alloc(storage[next].prev, next, content);
}


void line_free(LINENR linenr) {
	assert(linenr != INVALID_LINE);
	LINE *line = &storage[linenr];
	
	if (line->d)
		free(line->d);
	line_unlink(linenr);
}

LINE *line_get(LINENR linenr, const char *caller) {
	if (linenr < 0 || linenr >= MAX_LINES) {
		fprintf(stderr, "%s: line_get(%d)\n", caller, linenr);
		return NULL;
	}
	assert(linenr >= 0);
	assert(linenr < MAX_LINES);
	return &storage[linenr];
}

/* BUFFER MANAGEMENT *********************************************************/


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

	/* TODO: deallocate any previous lines */
	buffer->lines = INVALID_LINE;
	
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
		assert(linenr != INVALID_LINE);
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
	//	printf("|buffer->lines:%d",buffer->lines);
	assert(row >= 0 && row < INVALID_LINE);
	bufline = buffer->lines; /* Beginning of link list of lines of the buffer */
	while (row && bufline != INVALID_LINE) {
		bufline = storage[bufline].next;
		row--;
	}
	return bufline;
	
}

int buffer_count_lines(const BUFFER *buffer) {
	LINENR linenr = buffer_find_line_by_number(buffer, 0);
	assert(linenr != INVALID_LINE);

	int count;
	LINE *line;
	for (count = 1, line = line_get(linenr,"bufcntlines1"); line->next != INVALID_LINE; line = line_get(line->next,"bufcntlines2"))
		count++;
	return count;
}

void buffer_dump_lines(const BUFFER *buffer) {
	LINENR linenr;
	int i;

	for (linenr = buffer->lines, i=1; linenr != INVALID_LINE; linenr = storage[linenr].next, i++) {
		printf("%05d:%05d: %s\n", i, linenr, storage[linenr].d);
	}
}



/* Window management ***************************************************************/

WINDOW window[8];

WINDOW *window_create(BUFFER *buffer) {
	WINDOW *win;

	win = &window[0];
	win->buffer = buffer;
	win->cx = win->cy = win->t_line = win->t_col = 0;
	win->term = &vt52;
	assert(win->term);
	
	/* Assume full screen for now */
	{
		TERM_INFO ti;
		win->term->get_info(&ti);
		win->wx_min = 4;
		win->wx_max = ti.width-1;
		win->wy_min = 6;
		win->wy_max = ti.height-1;
	}

	return win;
}

void window_dump(const WINDOW *win) {
	printf("Window %p:\n",win);
	printf("  buffer: %p\n",win->buffer);
	printf("  term: %p\n",win->term);
	printf("  cx,cy: %d,%d\n",win->cx, win->cy);
	printf("  t_line,t_col: %d,%d\n", win->t_line, win->t_col);
	printf("   wx_min,wy_min,wx_max,wy_max: %d,%d,%d,%d\n",  win->wx_min,win->wy_min,win->wx_max,win->wy_max);
	printf("  allocated: %d\n",win->allocated);
}

void window_clear(const WINDOW *win) {
	int c,r;

	/* TODO: Speed this up */
	win->term->clear_screen();
	return;
	for (r=win->wy_min; r<=win->wy_max; r++) {
		win->term->set_cursor_position(win->wx_min, r);
		for (c=win->wx_min; c<=win->wx_max; c++) {
			win->term->printc(' ');
		}
	}
}

LINENR window_current_line_number(const WINDOW *win) {
	return win->t_line+win->cy;
}

LINE* window_current_line(const WINDOW *win) {
	LINENR line = buffer_find_line_by_number(win->buffer, window_current_line_number(win));
	assert(line != INVALID_LINE);
	return line_get(line,"wincurline");
}

void window_paint(const WINDOW *win) {
	int wr; /* Window row */
	int tr; /* Text row */
	int width = win->wx_max - win->wx_min + 1;

	int linelen;

	win->term->cursor_off();
	window_clear(win);
	tr = win->t_line;

	LINENR lnr = buffer_find_line_by_number(win->buffer,tr);
	printf("tr:%d, lnr:%d\n",tr,lnr);
	assert(lnr != INVALID_LINE && lnr < MAX_LINES);
	LINE *line = line_get(lnr,"winpaint");
	
	for (wr=win->wy_min; line && wr<=win->wy_max; wr++) {
		win->term->set_cursor_position(win->wx_min,wr); /* Beginning of the line */
		const char *l = line->d;
		linelen = l ? strlen(l) - win->t_col : 0; /* Number of chars to actually print on the row */
		int c = 0;

		if (linelen > 0) {

			if (linelen > width)
				linelen = width; /* Limit size to width of window */
			
			l = &l[win->t_col]; /* Apply horizontal scroll */
			
			for (; c<=linelen; c++) {
				win->term->printc(*l++); /* Print data from the line */
			}
		}
		else {
			/* We're scrolled to the right and all the text is out of view */
		}

		line = line_next(line);
	}

	/* If the window is active... */
	win->term->set_cursor_position(win->wx_min+win->cx, win->wy_min+win->cy);
	win->term->cursor_on();
}


/* MAIN ****************************************************************************/ 

int main(void) {
	line_init();

	BUFFER *buffer = buffer_create();
	if (!buffer)
		return ENOMEM;

	const char *filename = "text.txt";
	printf("Loading %s\n",filename);
	if (buffer_load(buffer, filename) != EOK) {
		printf("Erreur chargement du buffer\n");
		exit(0);
	}

	WINDOW *win  = window_create(buffer);
	window_paint(win);

	for (;;) {
		char data[MAX_LINE_LENGTH];
		char ascii;
		int scancode;
		int dead_keys;

		data[0] = '\0';
		
		win->term->get_key(&ascii, &scancode, &dead_keys);
		if (scancode == 0) /* No key pressed */
			continue;
		
		if (scancode == 1/*ESC*/)
			break;

		//		printf("scancode:%x\n",scancode);
		
		if (scancode == 0x4D/*RIGHT_ARROW*/ || scancode == 0x74/*CTRL+RIGHT*/) {
			LINE *line = window_current_line(win);
			assert(line);
			int len = strlen(line->d);
			if (win->cx < len) {
				if (scancode == 0x4D)
					win->cx++;
				else
					win->cx = len; 
				win->term->set_cursor_position(win->wx_min+win->cx, win->wy_min+win->cy);
			}
			else {
				if (buffer_find_line_by_number(win->buffer, win->t_line+win->cy+1) != INVALID_LINE) {
					win->cx = 0;
					win->cy++;
					win->term->set_cursor_position(win->wx_min+win->cx, win->wy_min+win->cy);
				}
			}
		}		
		else if (scancode == 0x4B/*LEFT_ARROW*/ || scancode==0x73/*CTRL+LEFT*/) {
			LINE *line = window_current_line(win);
			assert(line);

			if (win->cx > 0) {
				if (scancode == 0x73)
					win->cx = 0;
				else
					win->cx--;
				win->term->set_cursor_position(win->wx_min+win->cx, win->wy_min+win->cy);
			}
			else if (win->cy > 0) {
				LINENR prevln = buffer_find_line_by_number(win->buffer, win->t_line+win->cy-1);
				if (prevln != INVALID_LINE) {
					win->cx = strlen(line_get(prevln,"left")->d);
					win->cy--;
					win->term->set_cursor_position(win->wx_min+win->cx, win->wy_min+win->cy);
				}
			}
		}
		else if (scancode == 0x50/*DOWN_ARROW*/) {
			LINE *line = window_current_line(win);
			assert(line);
			if (win->cy < buffer_count_lines(win->buffer)-1) {
				LINE *next_line = line_next(line);
				int next_line_len = strlen(next_line->d);
				if (win->cx > next_line_len)
					win->cx = next_line_len;
				win->cy++;
				win->term->set_cursor_position(win->wx_min+win->cx, win->wy_min+win->cy);
			}
		}
		else if (scancode == 0x48/*UP_ARROW*/) {
			LINE *line = window_current_line(win);
			assert(line);
			if (win->cy > 0) {
				LINE *prev_line = line_prev(line);
				int prev_line_len = strlen(prev_line->d);
				if (win->cx > prev_line_len)
					win->cx = prev_line_len;
				win->cy--;
				win->term->set_cursor_position(win->wx_min+win->cx, win->wy_min+win->cy);
			}		
		}
		else if (scancode == 0x0E/*BACKSPACE*/) {
			LINE *line = window_current_line(win);			
			if (win->cx > 0) {
				/* Delete previous character on same line */
				strncpy(data, line->d, win->cx-1);
				data[win->cx-1] = '\0'; // strncpy doesn't do it !
				if (win->cx < strlen(line->d)) {
					win->term->set_cursor_position(0,2);
					strcat(data, &line->d[win->cx]);
				}			   
				line_set_text(line, data);
				win->cx--;
				window_paint(win);
			}
			else {
				// Append the line to any previous one
				if (win->cy > 0) {
					LINE *prev = line_prev(line);
					int prev_len = strlen(prev->d);
					if (strlen(line->d) + prev_len > MAX_LINE_LENGTH-1) {
						fprintf(stderr,"Line would be too long :(\n");
						continue;
					}
					strcpy(data, prev->d);
					strcat(data, line->d);
					line_set_text(prev,data);
					line_free(buffer_find_line_by_number(win->buffer, win->t_line+win->cy));
					win->cy--;
					win->cx = prev_len;
					window_paint(win);
				}
			}
		}
		else if (scancode == 0x53/*DELETE*/) {
			LINE *line = window_current_line(win);
			if (win->cx > 0) {
				/* The line has some data and we're not on the first column, so preserve everything before the cursor */
				strncpy(data, line->d, win->cx);
				data[win->cx] = '\0';
			}
			if (win->cx < strlen(line->d)) {				
				/* Delete next character on same line */
				strcat(data, &line->d[win->cx+1]);
				line_set_text(line,data);
			}
			else {
				/* We're at end of line, we have to concatenate any next line then delete it*/
				int lines_count = buffer_count_lines(win->buffer);
				if ((win->cy+1) != lines_count) {
					LINE* next = line_next(line);
					strcpy(data, line->d);
					strcat(data, next->d);
					line_set_text(line,data);
					line_free(buffer_find_line_by_number(win->buffer, win->t_line+win->cy+1));
					printf("LINE DELETED");Cnecin();
				}
			}
			window_paint(win);				
		}
		else if (scancode == 0x1C/*RETURN*/ || scancode == 0x72/*ENTER*/) {
			LINE *line = window_current_line(win);
			/* Copy text to previous line to be inserted */
			strcpy(data, &line->d[win->cx]);
			LINENR next_line_nr = line_insert_after(win->t_line+win->cy, data);
			/* Shorten */
			strncpy(data, line->d, win->cx);
			data[win->cx] = '\0';
			line_set_text(line, data);				
			if (win->cy <= (win->wy_max-win->wy_min+1)) {
				win->cy++;
			}
			win->cx = 0;
			window_paint(win);
		}
		else {
			/* Edit line */
			LINE *line = window_current_line(win);
			data[0] = '\0';
			strncpy(data, line->d, win->cx);
			data[win->cx] = ascii;
			data[win->cx+1] = '\0';
			strcat(data, &line->d[win->cx]);
			if (line_set_text(line, data) == EOK) {
				win->cx++;
				window_paint(win);
			}
		}
	}

	return EOK;
}
