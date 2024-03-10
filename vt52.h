#ifndef VT52_H
#define VT52_H

#include "editeur.h"

/* As returned by Kbshift() */
#define DEADKEY_LSHIFT 1
#define DEADKEY_RSHIFT 2
#define DEADKEY_CTRL 4
#define DEADKEY_ALT 8

extern TERM vt52;

#endif
