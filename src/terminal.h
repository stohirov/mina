/* terminal.h - talk to the terminal: raw mode, key presses, window size. */
#ifndef TERMINAL_H
#define TERMINAL_H

/* Ctrl+letter clears the top three bits of the letter, so Ctrl-S is 19. */
#define CTRL_KEY(k) ((k) & 0x1f)

/* Normal keys are returned as their byte value. Special keys arrive as
 * several bytes, so we give them values that no single byte can have. */
enum Key {
    KEY_NONE = 0,
    KEY_ENTER = 13,
    KEY_ESC = 27,
    KEY_BACKSPACE = 127,
    KEY_UP = 1000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_DELETE
};

/* Switch to raw mode and a separate screen. Returns -1 if not on a terminal. */
int terminal_enter(void);

/* Restore the terminal. Runs automatically at exit; safe to call twice. */
void terminal_leave(void);

/* Wait for the next key press and return it. */
int terminal_read_key(void);

void terminal_size(int *rows, int *cols);

#endif
