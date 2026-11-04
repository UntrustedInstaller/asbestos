#ifndef STRING32_H
#define STRING32_H

#include "types32.h"

void *memset(void *dst, int value, usize count);
void *memcpy(void *dst, const void *src, usize count);
void *memmove(void *dst, const void *src, usize count);
usize strlen(const char *s);
int strcmp(const char *a, const char *b);

#endif
