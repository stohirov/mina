/* The open file: its text, cursor and scroll position. Knows nothing about
 * drawing. Rows and columns start at 0. */
#ifndef EDITOR_H
#define EDITOR_H

#include "buffer.h"

#define TAB_WIDTH 4

typedef enum {
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN,
    MOVE_LINE_START,
    MOVE_LINE_END,
    MOVE_PAGE_UP,
    MOVE_PAGE_DOWN
} Move;

typedef struct {
    char *path;
    Buffer buffer;
    int row, col;  /* Cursor position in the text. */
    int top, left; /* First visible row and first visible screen column. */
} Editor;

/* Returns -1 with errno set if the file cannot be read. */
int editor_open(Editor *e, const char *path);
void editor_close(Editor *e);

/* Returns -1 with errno set on failure, and then stays dirty. */
int editor_save(Editor *e);
void editor_set_path(Editor *e, const char *path);

void editor_insert_char(Editor *e, char c);
void editor_insert_newline(Editor *e);
void editor_backspace(Editor *e);
void editor_delete_forward(Editor *e);
void editor_move(Editor *e, Move move, int page_rows);

/* Put the cursor at a text row and screen column. */
void editor_click(Editor *e, int row, int screen_col);

/* Where a text column appears on screen, since tabs are wider than one byte. */
int editor_screen_col(const Editor *e, int row, int col);

void editor_keep_cursor_visible(Editor *e, int rows, int cols);

#endif
