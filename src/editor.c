#include "editor.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "fs.h"
#include "terminal.h"
#include "theme.h"
#include "ui.h"

#define TAB_WIDTH 4

typedef struct {
    const char *path;
    Buffer buffer;
    int row, col;  /* Cursor position in the text. */
    int top, left; /* The first line and first column shown on screen. */
} Editor;

/* The two bottom rows are the status bar and the message line. */
static int text_rows(void) {
    int rows, cols;
    terminal_size(&rows, &cols);
    return rows > 2 ? rows - 2 : 1;
}

/* A tab takes up several columns on screen but only one byte in the line, so
 * the cursor's screen position has to be worked out. */
static int screen_col(const Line *line, int col) {
    int result = 0;
    for (int i = 0; i < col; i++) {
        if (line->text[i] == '\t') {
            result += TAB_WIDTH - result % TAB_WIDTH;
        } else {
            result++;
        }
    }
    return result;
}

/* Move the visible window just enough to keep the cursor inside it. */
static void scroll_to_cursor(Editor *e) {
    int rows, cols;
    terminal_size(&rows, &cols);
    int visible_rows = text_rows();

    if (e->row < e->top) e->top = e->row;
    if (e->row >= e->top + visible_rows) e->top = e->row - visible_rows + 1;

    int x = screen_col(&e->buffer.lines[e->row], e->col);
    if (x < e->left) e->left = x;
    if (x >= e->left + cols) e->left = x - cols + 1;
}

/* Print the part of a line that fits between left and left + width. */
static void draw_line(const Line *line, int left, int width) {
    int x = 0;
    for (int i = 0; i < line->len && x < left + width; i++) {
        unsigned char c = (unsigned char)line->text[i];
        if (c == '\t') {
            int spaces = TAB_WIDTH - x % TAB_WIDTH;
            for (int s = 0; s < spaces; s++, x++) {
                if (x >= left && x < left + width) putchar(' ');
            }
        } else {
            if (x >= left) putchar(iscntrl(c) ? '?' : c);
            x++;
        }
    }
}

static void draw(const Editor *e) {
    int rows, cols;
    terminal_size(&rows, &cols);
    int visible_rows = text_rows();

    ui_begin_frame();
    for (int i = 0; i < visible_rows; i++) {
        ui_start_row(i + 1, theme_current()->text);
        int line_index = e->top + i;
        if (line_index < e->buffer.count) {
            draw_line(&e->buffer.lines[line_index], e->left, cols);
        }
    }

    char status[1024];
    snprintf(status, sizeof(status), "%s%s | Ln %d/%d, Col %d | %s theme",
             e->path, e->buffer.dirty ? " (modified)" : "",
             e->row + 1, e->buffer.count, e->col + 1, theme_current()->name);
    ui_draw_bar(status);
    ui_draw_message("Ctrl-S save | Ctrl-Q close | Ctrl-T theme");

    int x = screen_col(&e->buffer.lines[e->row], e->col);
    ui_end_frame(e->row - e->top + 1, x - e->left + 1);
}

/* Returns 0 on success, or -1 after telling the user what went wrong. */
static int save(Editor *e) {
    size_t len;
    char *text = buffer_to_text(&e->buffer, &len);
    int result = fs_write_file(e->path, text, len);
    int saved_errno = errno; /* free() may change errno. */
    free(text);

    if (result != 0) {
        ui_set_message("Save failed: %s", strerror(saved_errno));
        return -1;
    }
    e->buffer.dirty = 0;
    ui_set_message("Saved %zu bytes", len);
    return 0;
}

/* Returns 1 if the editor may close. Unsaved work is never lost silently. */
static int confirm_close(Editor *e) {
    while (e->buffer.dirty) {
        int key = ui_ask_key("Save changes? y = save, n = discard, Esc = keep editing");
        if (key == 'y' || key == 'Y') return save(e) == 0;
        if (key == 'n' || key == 'N') return 1;
        if (key == KEY_ESC) return 0;
    }
    return 1;
}

static void move_cursor(Editor *e, int key) {
    int line_len = e->buffer.lines[e->row].len;
    int last_row = e->buffer.count - 1;

    switch (key) {
        case KEY_LEFT:
            if (e->col > 0) {
                e->col--;
            } else if (e->row > 0) {
                e->row--;
                e->col = e->buffer.lines[e->row].len;
            }
            break;
        case KEY_RIGHT:
            if (e->col < line_len) {
                e->col++;
            } else if (e->row < last_row) {
                e->row++;
                e->col = 0;
            }
            break;
        case KEY_UP:
            if (e->row > 0) e->row--;
            break;
        case KEY_DOWN:
            if (e->row < last_row) e->row++;
            break;
        case KEY_PAGE_UP:
            e->row -= text_rows();
            if (e->row < 0) e->row = 0;
            break;
        case KEY_PAGE_DOWN:
            e->row += text_rows();
            if (e->row > last_row) e->row = last_row;
            break;
        case KEY_HOME:
            e->col = 0;
            break;
        case KEY_END:
            e->col = line_len;
            break;
    }

    /* The line we landed on may be shorter than the column we were at. */
    if (e->col > e->buffer.lines[e->row].len) {
        e->col = e->buffer.lines[e->row].len;
    }
}

static void backspace(Editor *e) {
    if (e->col > 0) {
        e->col--;
        buffer_delete_char(&e->buffer, e->row, e->col);
    } else if (e->row > 0) {
        e->row--;
        e->col = e->buffer.lines[e->row].len;
        buffer_join_with_next(&e->buffer, e->row);
    }
}

static void delete_forward(Editor *e) {
    if (e->col < e->buffer.lines[e->row].len) {
        buffer_delete_char(&e->buffer, e->row, e->col);
    } else if (e->row < e->buffer.count - 1) {
        buffer_join_with_next(&e->buffer, e->row);
    }
}

int editor_run(const char *path) {
    char *text;
    size_t len;
    if (fs_read_file(path, &text, &len) != 0) return -1;

    Editor e = {0};
    e.path = path;
    buffer_init(&e.buffer, text, len);
    free(text);

    int open = 1;
    while (open) {
        scroll_to_cursor(&e);
        draw(&e);

        int key = terminal_read_key();
        switch (key) {
            case CTRL_KEY('s'):
                save(&e);
                break;
            case CTRL_KEY('q'):
                if (confirm_close(&e)) open = 0;
                break;
            case CTRL_KEY('t'):
                theme_toggle();
                break;
            case KEY_ENTER:
                buffer_split_line(&e.buffer, e.row, e.col);
                e.row++;
                e.col = 0;
                break;
            case KEY_BACKSPACE:
                backspace(&e);
                break;
            case KEY_DELETE:
                delete_forward(&e);
                break;
            case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT:
            case KEY_HOME: case KEY_END: case KEY_PAGE_UP: case KEY_PAGE_DOWN:
                move_cursor(&e, key);
                break;
            default:
                /* Tab, printable characters and the bytes of non-ASCII text.
                 * Everything else (other Ctrl keys, Esc) is ignored. */
                if (key == '\t' || (key >= 32 && key < 256)) {
                    buffer_insert_char(&e.buffer, e.row, e.col, (char)key);
                    e.col++;
                }
                break;
        }
    }

    buffer_free(&e.buffer);
    return 0;
}
