#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xrealloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size);
    if (result == NULL) {
        perror("out of memory");
        exit(1);
    }
    return result;
}

char *xstrdup(const char *text) {
    size_t size = strlen(text) + 1;
    return memcpy(xrealloc(NULL, size), text, size);
}
