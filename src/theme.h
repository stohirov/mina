/* theme.h - the light and dark color themes. */
#ifndef THEME_H
#define THEME_H

/* Each color is the escape sequence that tells the terminal to switch to it. */
typedef struct {
    const char *name;
    const char *text;     /* Normal text. */
    const char *bar;      /* The status bar. */
    const char *selected; /* The highlighted row in the file list. */
} Theme;

const Theme *theme_current(void);
void theme_toggle(void);

#endif
