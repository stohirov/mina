#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Allocation helpers that exit the program when out of memory. */
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *text);

#endif
