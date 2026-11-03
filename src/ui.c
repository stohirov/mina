#include "ui.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "terminal.h"
#include "theme.h"

static char message[512] = "";

void ui_begin_frame(void) {
    printf("\x1b[?25l"); /* Hide the cursor while we draw. */
}

void ui_end_frame(int cursor_row, int cursor_col) {
    if (cursor_row > 0) {
        printf("\x1b[%d;%dH\x1b[?25h", cursor_row, cursor_col);
    }
    fflush(stdout); /* Nothing reaches the screen until this line. */
}

void ui_start_row(int row, const char *color) {
    /* ESC [ row ; col H moves the cursor. ESC [ K clears to the end of the
     * line, filling it with the current background color. */
    printf("\x1b[%d;1H%s\x1b[K", row, color);
}

void ui_put_text(const char *text, int max_width) {
    for (int i = 0; text[i] != '\0' && i < max_width; i++) {
        unsigned char c = (unsigned char)text[i];
        /* File names and file contents could hold escape sequences. Printing
         * them raw would let a file control your terminal. */
        putchar(iscntrl(c) ? '?' : c);
    }
}

void ui_draw_bar(const char *text) {
    int rows, cols;
    terminal_size(&rows, &cols);
    ui_start_row(rows > 1 ? rows - 1 : 1, theme_current()->bar);
    ui_put_text(text, cols);
}

void ui_draw_message(const char *help) {
    int rows, cols;
    terminal_size(&rows, &cols);
    ui_start_row(rows, theme_current()->text);
    ui_put_text(message[0] != '\0' ? message : help, cols);
    message[0] = '\0';
}

void ui_set_message(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
}

/* Draw "text" on the last row, keeping its end visible if it is too long. */
static void draw_input_row(const char *text) {
    int rows, cols;
    terminal_size(&rows, &cols);
    int width = cols > 1 ? cols - 1 : 1;
    int len = (int)strlen(text);
    int start = len > width ? len - width : 0;

    ui_begin_frame();
    ui_start_row(rows, theme_current()->text);
    ui_put_text(text + start, width);
    ui_end_frame(rows, len - start + 1);
}

int ui_prompt(const char *label, char *out, size_t size) {
    size_t len = 0;
    out[0] = '\0';

    for (;;) {
        char line[1024];
        snprintf(line, sizeof(line), "%s: %s", label, out);
        draw_input_row(line);

        int key = terminal_read_key();
        if (key == KEY_ESC) {
            return 0;
        } else if (key == KEY_ENTER) {
            if (len > 0) return 1;
        } else if (key == KEY_BACKSPACE) {
            if (len > 0) out[--len] = '\0';
        } else if (key >= 32 && key < 256 && len + 1 < size) {
            out[len++] = (char)key;
            out[len] = '\0';
        }
    }
}

int ui_ask_key(const char *question) {
    draw_input_row(question);
    return terminal_read_key();
}
