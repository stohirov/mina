#include "theme.h"

/* ESC [ 38;5;N m sets the text color and ESC [ 48;5;N m sets the background,
 * where N is one of the terminal's 256 colors. Try changing the numbers! */
static const Theme dark = {
    .name = "dark",
    .text = "\x1b[38;5;252;48;5;234m",
    .bar = "\x1b[38;5;234;48;5;250m",
    .selected = "\x1b[38;5;231;48;5;25m",
};

static const Theme light = {
    .name = "light",
    .text = "\x1b[38;5;235;48;5;231m",
    .bar = "\x1b[38;5;231;48;5;240m",
    .selected = "\x1b[38;5;16;48;5;153m",
};

static const Theme *current = &dark;

const Theme *theme_current(void) {
    return current;
}

void theme_toggle(void) {
    current = (current == &dark) ? &light : &dark;
}
