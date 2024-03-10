#include <osbind.h>

#include "vt52.h"

static void vt52_output(const char *s);
static void vt52_clear_screen(void);

static void vt52_output(const char *s) { Cconws(s); } /* Cconws will interpret VT-52 codes */
static void vt52_printc(char c) { Crawio((unsigned char)c); } /* Crawio doesn't interpret VT-52 codes. */
static void vt52_init(void) { vt52_clear_screen(); }
static void vt52_deinit(void) { }
static void vt52_clear_screen(void) { vt52_output("\033E"); }
static void vt52_cursor_on(void) { vt52_output("\033e"); }
static void vt52_cursor_off(void) { vt52_output("\033f"); }
static char vt52curposbuf[] = "\033Yyx";
static void vt52_set_cursor_position(int x, int y) {
	vt52curposbuf[2] = (char)(y + 32);
	vt52curposbuf[3] = (char)(x + 32);
	vt52_output(vt52curposbuf);
}
static void vt52_get_key(char *ascii, int *scancode, int *dead_keys) {
	long ret = Crawio(0xff);
	*ascii = ret & 0x000000ff;
	*scancode = (ret & 0x00ff0000) >> 16;
	*dead_keys = Kbshift(-1);
}
static void vt52_get_info(TERM_INFO *terminfo) {
	terminfo->width = 80; /* TODO */
	terminfo->height = 25;
	terminfo->ncolors = 1;
}

TERM vt52 = {
	vt52_init,
	vt52_deinit,
	vt52_clear_screen,
	vt52_set_cursor_position,
	vt52_cursor_on,
	vt52_cursor_off,
	vt52_get_key,
	vt52_printc,
	vt52_output, /* CAVEAT: this interprets VT-52 escape sequences */
	vt52_get_info
};

