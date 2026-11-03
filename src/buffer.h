#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

typedef struct {
    char *text; /* Always ends with '\0'. */
    int len;    /* Bytes, not counting the '\0'. */
} Line;

typedef struct {
    Line *lines; /* Always at least one line. */
    int count;
    int dirty;   /* 1 if there are unsaved changes. */
} Buffer;

typedef struct {
    int row, col;
} Pos;

/* Split text on '\n'. buffer_to_text gives back exactly the same bytes. */
void buffer_init(Buffer *b, const char *text, size_t len);
void buffer_free(Buffer *b);

/* The caller must free() the results. A range includes start, not end. */
char *buffer_to_text(const Buffer *b, size_t *out_len);
char *buffer_text_range(const Buffer *b, Pos start, Pos end, size_t *out_len);

/* Returns the position just after the inserted text. */
Pos buffer_insert_text(Buffer *b, Pos at, const char *text, size_t len);
void buffer_delete_range(Buffer *b, Pos start, Pos end);

#endif
