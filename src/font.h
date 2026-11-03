/* A monospace font drawn from a texture holding the printable ASCII characters.
 * Anything else, such as non-English text, is drawn as '?'. */
#ifndef FONT_H
#define FONT_H

#include <SDL3/SDL.h>

#include "theme.h"

typedef struct {
    SDL_Renderer *renderer;
    SDL_Texture *atlas;
    int cell_w, cell_h;
} Font;

/* Returns -1 and sets the SDL error if no usable font is found. */
int font_load(Font *font, SDL_Renderer *renderer, int pixel_size);
void font_free(Font *font);

void font_draw_char(const Font *font, int x, int y, char c, Color color);
void font_draw_text(const Font *font, int x, int y, const char *text, Color color);

#endif
