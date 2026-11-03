/* buffer.h - the text being edited, stored as an array of lines. */
#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

typedef struct {
    char *text; /* Heap memory, always ends with '\0'. */
    int len;    /* Bytes in the line, not counting the '\0'. */
} Line;

typedef struct {
    Line *lines; /* Heap array. There is always at least one line. */
    int count;
    int dirty;   /* 1 if there are changes that have not been saved. */
} Buffer;

/* Split text on '\n'. Saving later gives back exactly the same bytes. */
void buffer_init(Buffer *b, const char *text, size_t len);
void buffer_free(Buffer *b);

/* Join all lines into one new string. The caller must free() it. */
char *buffer_to_text(const Buffer *b, size_t *out_len);

void buffer_insert_char(Buffer *b, int row, int col, char c);
void buffer_delete_char(Buffer *b, int row, int col);

/* Pressing Enter: everything after col moves to a new line below. */
void buffer_split_line(Buffer *b, int row, int col);

/* Append the line below onto this one. */
void buffer_join_with_next(Buffer *b, int row);

#endif
