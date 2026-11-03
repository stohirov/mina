#include "editor.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "util.h"

int editor_open(Editor *e, const char *path) {
    char *text;
    size_t len;
    if (fs_read_file(path, &text, &len) != 0) return -1;

    memset(e, 0, sizeof(*e));
    e->path = xstrdup(path);
    buffer_init(&e->buffer, text, len);
    free(text);
    return 0;
}

void editor_close(Editor *e) {
    buffer_free(&e->buffer);
    free(e->path);
    e->path = NULL;
}

int editor_save(Editor *e) {
    size_t len;
    char *text = buffer_to_text(&e->buffer, &len);
    int result = fs_write_file(e->path, text, len);
    int saved_errno = errno;
    free(text);
    if (result != 0) {
        errno = saved_errno;
        return -1;
    }
    e->buffer.dirty = 0;
    return 0;
}

void editor_set_path(Editor *e, const char *path) {
    char *copy = xstrdup(path);
    free(e->path);
    e->path = copy;
}

void editor_insert_char(Editor *e, char c) {
    buffer_insert_char(&e->buffer, e->row, e->col, c);
    e->col++;
}

void editor_insert_newline(Editor *e) {
    buffer_split_line(&e->buffer, e->row, e->col);
    e->row++;
    e->col = 0;
}

void editor_backspace(Editor *e) {
    if (e->col > 0) {
        e->col--;
        buffer_delete_char(&e->buffer, e->row, e->col);
    } else if (e->row > 0) {
        e->row--;
        e->col = e->buffer.lines[e->row].len;
        buffer_join_with_next(&e->buffer, e->row);
    }
}

void editor_delete_forward(Editor *e) {
    if (e->col < e->buffer.lines[e->row].len) {
        buffer_delete_char(&e->buffer, e->row, e->col);
    } else if (e->row < e->buffer.count - 1) {
        buffer_join_with_next(&e->buffer, e->row);
    }
}

void editor_move(Editor *e, Move move, int page_rows) {
    int line_len = e->buffer.lines[e->row].len;
    int last_row = e->buffer.count - 1;

    switch (move) {
        case MOVE_LEFT:
            if (e->col > 0) {
                e->col--;
            } else if (e->row > 0) {
                e->row--;
                e->col = e->buffer.lines[e->row].len;
            }
            break;
        case MOVE_RIGHT:
            if (e->col < line_len) {
                e->col++;
            } else if (e->row < last_row) {
                e->row++;
                e->col = 0;
            }
            break;
        case MOVE_UP:
            if (e->row > 0) e->row--;
            break;
        case MOVE_DOWN:
            if (e->row < last_row) e->row++;
            break;
        case MOVE_LINE_START:
            e->col = 0;
            break;
        case MOVE_LINE_END:
            e->col = line_len;
            break;
        case MOVE_PAGE_UP:
            e->row -= page_rows;
            if (e->row < 0) e->row = 0;
            break;
        case MOVE_PAGE_DOWN:
            e->row += page_rows;
            if (e->row > last_row) e->row = last_row;
            break;
    }

    if (e->col > e->buffer.lines[e->row].len) {
        e->col = e->buffer.lines[e->row].len;
    }
}

/* How many screen columns the byte at text position x takes up. */
static int char_width(char c, int x) {
    return c == '\t' ? TAB_WIDTH - x % TAB_WIDTH : 1;
}

int editor_screen_col(const Editor *e, int row, int col) {
    const Line *line = &e->buffer.lines[row];
    int x = 0;
    for (int i = 0; i < col; i++) {
        x += char_width(line->text[i], x);
    }
    return x;
}

void editor_click(Editor *e, int row, int screen_col) {
    if (row < 0) row = 0;
    if (row >= e->buffer.count) row = e->buffer.count - 1;

    const Line *line = &e->buffer.lines[row];
    int x = 0;
    int col = 0;
    while (col < line->len) {
        x += char_width(line->text[col], x);
        if (screen_col < x) break;
        col++;
    }
    e->row = row;
    e->col = col;
}

void editor_keep_cursor_visible(Editor *e, int rows, int cols) {
    if (e->row < e->top) e->top = e->row;
    if (e->row >= e->top + rows) e->top = e->row - rows + 1;

    int x = editor_screen_col(e, e->row, e->col);
    if (x < e->left) e->left = x;
    if (x >= e->left + cols) e->left = x - cols + 1;
}
