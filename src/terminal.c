#include "terminal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios original_settings;
static int is_active = 0;

void terminal_leave(void) {
    if (!is_active) return;
    is_active = 0;
    /* Reset colors, show the cursor, leave the separate screen. */
    printf("\x1b[0m\x1b[?25h\x1b[?1049l");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_settings);
}

int terminal_enter(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &original_settings) != 0) return -1;

    /* By default the terminal waits for Enter, echoes what you type and turns
     * Ctrl-C into a signal. An editor wants every key immediately, so we turn
     * those features off. This is called "raw mode". */
    struct termios raw = original_settings;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* read() waits at most 0.1 seconds, then returns even with no input. */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;

    is_active = 1;
    atexit(terminal_leave);

    /* Collect each frame in memory and send it in one go, to avoid flicker. */
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);

    /* The "alternate screen" gives your shell history back when we quit. */
    printf("\x1b[?1049h");
    fflush(stdout);
    return 0;
}

/* Returns 1 and stores a byte if one arrives within the read() timeout. */
static int read_byte(char *c) {
    return read(STDIN_FILENO, c, 1) == 1;
}

/* Special keys are sent as "escape sequences", for example the Up arrow is the
 * three bytes ESC [ A, and Delete is the four bytes ESC [ 3 ~. */
static int read_escape_sequence(void) {
    char first;
    if (!read_byte(&first)) return KEY_ESC; /* Just the Esc key on its own. */

    if (first == 'O') { /* Some terminals send Home and End this way. */
        char second;
        if (!read_byte(&second)) return KEY_NONE;
        if (second == 'H') return KEY_HOME;
        if (second == 'F') return KEY_END;
        return KEY_NONE;
    }
    if (first != '[') return KEY_NONE; /* Alt+key and friends: ignored. */

    /* A sequence ends with a byte from '@' to '~'. Reading up to that byte
     * means unknown keys are skipped whole, instead of leaving stray
     * characters behind that would be typed into your file. */
    char seq[16];
    int len = 0;
    char c;
    while (read_byte(&c)) {
        if (c >= '@' && c <= '~') {
            seq[len] = '\0';
            switch (c) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                case '~':
                    switch (atoi(seq)) {
                        case 1: case 7: return KEY_HOME;
                        case 3: return KEY_DELETE;
                        case 4: case 8: return KEY_END;
                        case 5: return KEY_PAGE_UP;
                        case 6: return KEY_PAGE_DOWN;
                    }
            }
            return KEY_NONE;
        }
        if (len < (int)sizeof(seq) - 1) seq[len++] = c;
    }
    return KEY_NONE;
}

int terminal_read_key(void) {
    /* unsigned, so that bytes above 127 do not turn into negative numbers. */
    unsigned char c;
    ssize_t n;
    while ((n = read(STDIN_FILENO, &c, 1)) != 1) {
        if (n == -1 && errno != EAGAIN && errno != EINTR) exit(1);
    }
    if (c == KEY_ESC) return read_escape_sequence();
    if (c == '\n') return KEY_ENTER;
    if (c == 8) return KEY_BACKSPACE; /* Some terminals send Ctrl-H for it. */
    return c;
}

void terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}
