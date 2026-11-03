#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* malloc and realloc return NULL when memory runs out. This editor cannot do
 * anything useful at that point, so we stop in one place instead of checking
 * after every call. */
static void *checked_realloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size);
    if (result == NULL) {
        perror("out of memory");
        exit(1);
    }
    return result;
}

/* Make a Line holding a copy of len bytes from text. */
static Line make_line(const char *text, size_t len) {
    Line line;
    line.text = checked_realloc(NULL, len + 1);
    if (len > 0) memcpy(line.text, text, len);
    line.text[len] = '\0';
    line.len = (int)len;
    return line;
}

void buffer_init(Buffer *b, const char *text, size_t len) {
    /* Count the lines first, so we can allocate the array once. */
    int count = 1;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') count++;
    }

    b->lines = checked_realloc(NULL, sizeof(Line) * count);
    b->count = 0;
    b->dirty = 0;

    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            b->lines[b->count++] = make_line(text + start, i - start);
            start = i + 1;
        }
    }
}

void buffer_free(Buffer *b) {
    for (int i = 0; i < b->count; i++) {
        free(b->lines[i].text);
    }
    free(b->lines);
    b->lines = NULL;
    b->count = 0;
}

char *buffer_to_text(const Buffer *b, size_t *out_len) {
    size_t total = 0;
    for (int i = 0; i < b->count; i++) {
        total += (size_t)b->lines[i].len + 1; /* +1 for '\n' or the final '\0'. */
    }

    char *text = checked_realloc(NULL, total);
    char *p = text;
    for (int i = 0; i < b->count; i++) {
        memcpy(p, b->lines[i].text, b->lines[i].len);
        p += b->lines[i].len;
        if (i < b->count - 1) *p++ = '\n';
    }
    *p = '\0';
    *out_len = (size_t)(p - text);
    return text;
}

void buffer_insert_char(Buffer *b, int row, int col, char c) {
    Line *line = &b->lines[row];
    line->text = checked_realloc(line->text, (size_t)line->len + 2);
    /* Shift the rest of the line, including its '\0', one byte to the right.
     * memmove (not memcpy) is required because the two areas overlap. */
    memmove(line->text + col + 1, line->text + col, (size_t)(line->len - col) + 1);
    line->text[col] = c;
    line->len++;
    b->dirty = 1;
}

void buffer_delete_char(Buffer *b, int row, int col) {
    Line *line = &b->lines[row];
    memmove(line->text + col, line->text + col + 1, (size_t)(line->len - col));
    line->len--;
    b->dirty = 1;
}

void buffer_split_line(Buffer *b, int row, int col) {
    Line tail = make_line(b->lines[row].text + col, (size_t)(b->lines[row].len - col));

    b->lines = checked_realloc(b->lines, sizeof(Line) * ((size_t)b->count + 1));
    memmove(&b->lines[row + 2], &b->lines[row + 1], sizeof(Line) * (size_t)(b->count - row - 1));
    b->lines[row + 1] = tail;
    b->count++;

    b->lines[row].text[col] = '\0';
    b->lines[row].len = col;
    b->dirty = 1;
}

void buffer_join_with_next(Buffer *b, int row) {
    Line *line = &b->lines[row];
    Line *next = &b->lines[row + 1];

    line->text = checked_realloc(line->text, (size_t)line->len + (size_t)next->len + 1);
    memcpy(line->text + line->len, next->text, (size_t)next->len + 1);
    line->len += next->len;
    free(next->text);

    memmove(next, next + 1, sizeof(Line) * (size_t)(b->count - row - 2));
    b->count--;
    b->dirty = 1;
}
