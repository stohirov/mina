#ifndef THEME_H
#define THEME_H

typedef struct {
    unsigned char r, g, b;
} Color;

typedef struct {
    const char *name;
    Color background;
    Color text;
    Color sidebar;
    Color selected;
    Color bar;
    Color button;
    Color cursor;
} Theme;

const Theme *theme_current(void);
void theme_toggle(void);

#endif
