#ifndef __EDL_H__
#define __EDL_H__

#include <stdbool.h>

struct edl;

struct edl *edl_open(const char *serial, void (*edl_present)(bool present));

#endif

