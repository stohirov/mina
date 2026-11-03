#include "buffer.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

static Line make_line(const char *text, size_t len) {
    Line line;
    line.text = xrealloc(NULL, len + 1);
    if (len > 0) memcpy(line.text, text, len);
    line.text[len] = '\0';
    line.len = (int)len;
    return line;
}

static void append_to_line(Line *line, const char *text, size_t len) {
    line->text = xrealloc(line->text, (size_t)line->len + len + 1);
    if (len > 0) memcpy(line->text + line->len, text, len);
    line->len += (int)len;
    line->text[line->len] = '\0';
}

void buffer_init(Buffer *b, const char *text, size_t len) {
    int count = 1;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') count++;
    }

    b->lines = xrealloc(NULL, sizeof(Line) * (size_t)count);
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

char *buffer_text_range(const Buffer *b, Pos start, Pos end, size_t *out_len) {
    size_t total = 0;
    for (int row = start.row; row <= end.row; row++) {
        int from = row == start.row ? start.col : 0;
        int to = row == end.row ? end.col : b->lines[row].len;
        total += (size_t)(to - from) + 1; /* '\n' or the final '\0'. */
    }

    char *text = xrealloc(NULL, total);
    char *p = text;
    for (int row = start.row; row <= end.row; row++) {
        int from = row == start.row ? start.col : 0;
        int to = row == end.row ? end.col : b->lines[row].len;
        memcpy(p, b->lines[row].text + from, (size_t)(to - from));
        p += to - from;
        if (row < end.row) *p++ = '\n';
    }
    *p = '\0';
    *out_len = total - 1;
    return text;
}

char *buffer_to_text(const Buffer *b, size_t *out_len) {
    Pos start = {0, 0};
    Pos end = {b->count - 1, b->lines[b->count - 1].len};
    return buffer_text_range(b, start, end, out_len);
}

Pos buffer_insert_text(Buffer *b, Pos at, const char *text, size_t len) {
    int newlines = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') newlines++;
    }

    Line *line = &b->lines[at.row];
    Line tail = make_line(line->text + at.col, (size_t)(line->len - at.col));
    line->text[at.col] = '\0';
    line->len = at.col;

    b->lines = xrealloc(b->lines, sizeof(Line) * ((size_t)b->count + (size_t)newlines));
    memmove(&b->lines[at.row + newlines + 1], &b->lines[at.row + 1],
            sizeof(Line) * (size_t)(b->count - at.row - 1));
    b->count += newlines;

    int row = at.row;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            if (start == 0) {
                append_to_line(&b->lines[row], text, i);
            } else {
                b->lines[++row] = make_line(text + start, i - start);
            }
            start = i + 1;
        }
    }

    Pos end = {row, b->lines[row].len};
    append_to_line(&b->lines[row], tail.text, (size_t)tail.len);
    free(tail.text);
    b->dirty = 1;
    return end;
}

void buffer_delete_range(Buffer *b, Pos start, Pos end) {
    Line *first = &b->lines[start.row];
    Line *last = &b->lines[end.row];
    size_t tail_len = (size_t)(last->len - end.col);

    if (start.row == end.row) {
        memmove(first->text + start.col, first->text + end.col, tail_len + 1);
    } else {
        first->text = xrealloc(first->text, (size_t)start.col + tail_len + 1);
        memcpy(first->text + start.col, last->text + end.col, tail_len + 1);
        for (int row = start.row + 1; row <= end.row; row++) {
            free(b->lines[row].text);
        }
        memmove(&b->lines[start.row + 1], &b->lines[end.row + 1],
                sizeof(Line) * (size_t)(b->count - end.row - 1));
        b->count -= end.row - start.row;
    }
    first->len = start.col + (int)tail_len;
    b->dirty = 1;
}
