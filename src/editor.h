/* The open file: its text, cursor, selection, undo history and scroll position.
 * Knows nothing about drawing. Rows and columns start at 0. */
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

/* Runs of the same kind of edit are undone together. */
typedef enum {
    EDIT_OTHER,
    EDIT_TYPING,
    EDIT_BACKSPACE,
    EDIT_DELETE
} EditKind;

/* What it takes to reverse one edit: replace start..inserted_end with removed. */
typedef struct {
    EditKind kind;
    Pos start;
    Pos inserted_end;
    char *removed;
    size_t removed_len;
    Pos cursor_before;
} UndoStep;

typedef struct {
    char *path;
    Buffer buffer;
    int row, col;  /* Cursor position in the text. */
    int top, left; /* First visible row and first visible screen column. */

    int selecting; /* If 1, the selection runs from anchor to the cursor. */
    Pos anchor;

    UndoStep *undo;
    int undo_count;
    int saved_undo_count; /* undo_count when last saved, or -1 if unreachable. */
    int can_merge;        /* 1 if the next edit may join the last undo step. */
} Editor;

/* Returns -1 with errno set if the file cannot be read. */
int editor_open(Editor *e, const char *path);
void editor_close(Editor *e);

/* Returns -1 with errno set on failure, and then stays dirty. */
int editor_save(Editor *e);
void editor_set_path(Editor *e, const char *path);

/* These replace the selection, if there is one. */
void editor_insert_char(Editor *e, char c);
void editor_insert_newline(Editor *e);
void editor_insert_text(Editor *e, const char *text, size_t len);
void editor_backspace(Editor *e);
void editor_delete_forward(Editor *e);

/* Returns 0 if there is nothing to undo. */
int editor_undo(Editor *e);

/* If extend is 1, the movement grows the selection. Otherwise it clears it. */
void editor_move(Editor *e, Move move, int page_rows, int extend);

/* Put the cursor at a text row and screen column. */
void editor_click(Editor *e, int row, int screen_col, int extend);

/* Returns 1 and the ordered range if some text is selected. */
int editor_selection(const Editor *e, Pos *start, Pos *end);

/* Returns the selected text, or NULL if none. The caller must free() it. */
char *editor_selected_text(const Editor *e);

/* Where a text column appears on screen, since tabs are wider than one byte. */
int editor_screen_col(const Editor *e, int row, int col);

void editor_keep_cursor_visible(Editor *e, int rows, int cols);

#endif
