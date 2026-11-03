#include "editor.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "util.h"

#define MAX_UNDO_STEPS 1000

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
    for (int i = 0; i < e->undo_count; i++) {
        free(e->undo[i].removed);
    }
    free(e->undo);
    e->undo = NULL;
    e->undo_count = 0;
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
    e->saved_undo_count = e->undo_count;
    e->can_merge = 0;
    return 0;
}

void editor_set_path(Editor *e, const char *path) {
    char *copy = xstrdup(path);
    free(e->path);
    e->path = copy;
}

/* ---------- Undo history ---------- */

static Pos cursor_pos(const Editor *e) {
    return (Pos){e->row, e->col};
}

static void set_cursor(Editor *e, Pos pos) {
    e->row = pos.row;
    e->col = pos.col;
}

static void push_undo_step(Editor *e, UndoStep step) {
    /* The saved state was undone and is now being replaced: it can't return. */
    if (e->saved_undo_count > e->undo_count) e->saved_undo_count = -1;

    if (e->undo_count == MAX_UNDO_STEPS) {
        free(e->undo[0].removed);
        memmove(&e->undo[0], &e->undo[1], sizeof(UndoStep) * (MAX_UNDO_STEPS - 1));
        e->undo_count--;
        if (e->saved_undo_count >= 0) e->saved_undo_count--;
    }

    e->undo = xrealloc(e->undo, sizeof(UndoStep) * ((size_t)e->undo_count + 1));
    e->undo[e->undo_count++] = step;
}

/* Add text to the removed text of a step, before or after what it holds. */
static void add_removed(UndoStep *step, const char *text, size_t len, int before) {
    char *joined = xrealloc(NULL, step->removed_len + len + 1);
    const char *first = before ? text : step->removed;
    size_t first_len = before ? len : step->removed_len;
    const char *second = before ? step->removed : text;
    size_t second_len = before ? step->removed_len : len;

    memcpy(joined, first, first_len);
    memcpy(joined + first_len, second, second_len);
    joined[step->removed_len + len] = '\0';
    free(step->removed);
    step->removed = joined;
    step->removed_len += len;
}

/* Words are undone one at a time, so a space starts a new step. */
static int can_join_last_step(const Editor *e, EditKind kind, int replaced_text, char first_char) {
    if (!e->can_merge || e->undo_count == 0 || kind == EDIT_OTHER) return 0;
    if (e->undo[e->undo_count - 1].kind != kind) return 0;
    if (kind == EDIT_TYPING) return !replaced_text && first_char != ' ';
    return 1;
}

/* Every change to the text goes through here, which is what makes it undoable. */
static void replace_range(Editor *e, Pos start, Pos end, const char *text, size_t len, EditKind kind) {
    int has_range = start.row != end.row || start.col != end.col;
    if (!has_range && len == 0) return;

    size_t removed_len;
    char *removed = buffer_text_range(&e->buffer, start, end, &removed_len);
    Pos cursor_before = cursor_pos(e);

    if (has_range) buffer_delete_range(&e->buffer, start, end);
    Pos inserted_end = len > 0 ? buffer_insert_text(&e->buffer, start, text, len) : start;

    char first_char = len > 0 ? text[0] : '\0';
    if (can_join_last_step(e, kind, has_range, first_char)) {
        UndoStep *last = &e->undo[e->undo_count - 1];
        if (kind == EDIT_BACKSPACE) {
            add_removed(last, removed, removed_len, 1);
            last->start = start;
            last->inserted_end = start;
        } else if (kind == EDIT_DELETE) {
            add_removed(last, removed, removed_len, 0);
        } else {
            last->inserted_end = inserted_end;
        }
        free(removed);
    } else {
        UndoStep step = {kind, start, inserted_end, removed, removed_len, cursor_before};
        push_undo_step(e, step);
    }

    set_cursor(e, inserted_end);
    e->selecting = 0;
    e->can_merge = 1;
}

int editor_undo(Editor *e) {
    if (e->undo_count == 0) return 0;

    UndoStep step = e->undo[--e->undo_count];
    buffer_delete_range(&e->buffer, step.start, step.inserted_end);
    if (step.removed_len > 0) {
        buffer_insert_text(&e->buffer, step.start, step.removed, step.removed_len);
    }
    free(step.removed);

    set_cursor(e, step.cursor_before);
    e->selecting = 0;
    e->can_merge = 0;
    e->buffer.dirty = e->undo_count != e->saved_undo_count;
    return 1;
}

/* ---------- Editing ---------- */

int editor_selection(const Editor *e, Pos *start, Pos *end) {
    if (!e->selecting) return 0;

    Pos cursor = cursor_pos(e);
    int anchor_first = e->anchor.row < cursor.row ||
                       (e->anchor.row == cursor.row && e->anchor.col < cursor.col);
    if (e->anchor.row == cursor.row && e->anchor.col == cursor.col) return 0;

    *start = anchor_first ? e->anchor : cursor;
    *end = anchor_first ? cursor : e->anchor;
    return 1;
}

char *editor_selected_text(const Editor *e) {
    Pos start, end;
    if (!editor_selection(e, &start, &end)) return NULL;
    size_t len;
    return buffer_text_range(&e->buffer, start, end, &len);
}

/* Insert at the cursor, replacing the selection if there is one. */
static void insert_at_cursor(Editor *e, const char *text, size_t len, EditKind kind) {
    Pos start, end;
    if (!editor_selection(e, &start, &end)) start = end = cursor_pos(e);
    replace_range(e, start, end, text, len, kind);
}

void editor_insert_char(Editor *e, char c) {
    insert_at_cursor(e, &c, 1, EDIT_TYPING);
}

void editor_insert_newline(Editor *e) {
    insert_at_cursor(e, "\n", 1, EDIT_OTHER);
}

void editor_insert_text(Editor *e, const char *text, size_t len) {
    insert_at_cursor(e, text, len, EDIT_OTHER);
}

void editor_backspace(Editor *e) {
    Pos start, end;
    if (editor_selection(e, &start, &end)) {
        replace_range(e, start, end, "", 0, EDIT_OTHER);
        return;
    }

    end = cursor_pos(e);
    if (e->col > 0) {
        start = (Pos){e->row, e->col - 1};
    } else if (e->row > 0) {
        start = (Pos){e->row - 1, e->buffer.lines[e->row - 1].len};
    } else {
        return;
    }
    replace_range(e, start, end, "", 0, EDIT_BACKSPACE);
}

void editor_delete_forward(Editor *e) {
    Pos start, end;
    if (editor_selection(e, &start, &end)) {
        replace_range(e, start, end, "", 0, EDIT_OTHER);
        return;
    }

    start = cursor_pos(e);
    if (e->col < e->buffer.lines[e->row].len) {
        end = (Pos){e->row, e->col + 1};
    } else if (e->row < e->buffer.count - 1) {
        end = (Pos){e->row + 1, 0};
    } else {
        return;
    }
    replace_range(e, start, end, "", 0, EDIT_DELETE);
}

/* ---------- Cursor ---------- */

/* Start or clear the selection, before the cursor moves. */
static void prepare_selection(Editor *e, int extend) {
    if (extend && !e->selecting) e->anchor = cursor_pos(e);
    e->selecting = extend;
    e->can_merge = 0;
}

void editor_move(Editor *e, Move move, int page_rows, int extend) {
    /* Left or Right with a selection goes to that end of it. */
    Pos start, end;
    if (!extend && (move == MOVE_LEFT || move == MOVE_RIGHT) && editor_selection(e, &start, &end)) {
        set_cursor(e, move == MOVE_LEFT ? start : end);
        prepare_selection(e, 0);
        return;
    }
    prepare_selection(e, extend);

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

void editor_click(Editor *e, int row, int screen_col, int extend) {
    prepare_selection(e, extend);

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
