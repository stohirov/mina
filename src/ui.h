/* ui.h - drawing helpers shared by the file browser and the editor. */
#ifndef UI_H
#define UI_H

#include <stddef.h>

/* Every screen update starts with begin and finishes with end. Pass a cursor
 * row of 0 to keep the cursor hidden. Rows and columns start at 1. */
void ui_begin_frame(void);
void ui_end_frame(int cursor_row, int cursor_col);

/* Move to a row, set its color and clear it. */
void ui_start_row(int row, const char *color);

/* Print at most max_width bytes, replacing control characters with '?'. */
void ui_put_text(const char *text, int max_width);

/* Draw the status bar on the row above the last one. */
void ui_draw_bar(const char *text);

/* Draw the last row: the pending message if there is one, otherwise help. */
void ui_draw_message(const char *help);

/* Set a message to show until the next key press. Works like printf. */
void ui_set_message(const char *format, ...);

/* Ask for a line of text. Returns 1 on Enter, or 0 if cancelled with Esc. */
int ui_prompt(const char *label, char *out, size_t size);

/* Show a question and return the key that was pressed. */
int ui_ask_key(const char *question);

#endif
