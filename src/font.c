#include "font.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#define FIRST_CHAR 32
#define LAST_CHAR 126
#define CHAR_COUNT (LAST_CHAR - FIRST_CHAR + 1)

static const char *const font_paths[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Monaco.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
};

static FT_Face open_font(FT_Library library) {
    for (size_t i = 0; i < SDL_arraysize(font_paths); i++) {
        FT_Face face;
        if (FT_New_Face(library, font_paths[i], 0, &face) == 0) return face;
    }
    return NULL;
}

/* White pixels, with the glyph's shape in alpha, so any color can be applied
 * when drawing. */
static void copy_glyph(SDL_Surface *surface, const FT_GlyphSlot glyph, int cell_x, int ascent) {
    const FT_Bitmap *bitmap = &glyph->bitmap;
    for (int row = 0; row < (int)bitmap->rows; row++) {
        int y = ascent - glyph->bitmap_top + row;
        if (y < 0 || y >= surface->h) continue;

        for (int col = 0; col < (int)bitmap->width; col++) {
            int x = cell_x + glyph->bitmap_left + col;
            if (x < 0 || x >= surface->w) continue;

            Uint8 *pixel = (Uint8 *)surface->pixels + y * surface->pitch + x * 4;
            pixel[3] = bitmap->buffer[row * bitmap->pitch + col];
        }
    }
}

int font_load(Font *font, SDL_Renderer *renderer, int pixel_size) {
    FT_Library library;
    if (FT_Init_FreeType(&library) != 0) {
        SDL_SetError("Cannot start FreeType");
        return -1;
    }

    int result = -1;
    SDL_Surface *surface = NULL;
    FT_Face face = open_font(library);
    if (face == NULL) {
        SDL_SetError("No monospace font found");
        goto done;
    }
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixel_size) != 0 ||
        FT_Load_Char(face, 'M', FT_LOAD_DEFAULT) != 0) {
        SDL_SetError("Cannot load font size");
        goto done;
    }

    font->renderer = renderer;
    font->cell_w = (int)(face->glyph->advance.x >> 6);
    font->cell_h = (int)(face->size->metrics.height >> 6);
    int ascent = (int)(face->size->metrics.ascender >> 6);

    surface = SDL_CreateSurface(font->cell_w * CHAR_COUNT, font->cell_h, SDL_PIXELFORMAT_RGBA32);
    if (surface == NULL) goto done;

    for (int y = 0; y < surface->h; y++) {
        SDL_memset((Uint8 *)surface->pixels + y * surface->pitch, 0xff, (size_t)surface->w * 4);
        for (int x = 0; x < surface->w; x++) {
            ((Uint8 *)surface->pixels)[y * surface->pitch + x * 4 + 3] = 0;
        }
    }
    for (int c = FIRST_CHAR; c <= LAST_CHAR; c++) {
        if (FT_Load_Char(face, (FT_ULong)c, FT_LOAD_RENDER) == 0) {
            copy_glyph(surface, face->glyph, (c - FIRST_CHAR) * font->cell_w, ascent);
        }
    }

    font->atlas = SDL_CreateTextureFromSurface(renderer, surface);
    if (font->atlas == NULL) goto done;
    SDL_SetTextureBlendMode(font->atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(font->atlas, SDL_SCALEMODE_NEAREST);
    result = 0;

done:
    SDL_DestroySurface(surface);
    if (face != NULL) FT_Done_Face(face);
    FT_Done_FreeType(library);
    return result;
}

void font_free(Font *font) {
    SDL_DestroyTexture(font->atlas);
    font->atlas = NULL;
}

void font_draw_char(const Font *font, int x, int y, char c, Color color) {
    int index = (c >= FIRST_CHAR && c <= LAST_CHAR) ? c - FIRST_CHAR : '?' - FIRST_CHAR;
    SDL_FRect source = {(float)(index * font->cell_w), 0, (float)font->cell_w, (float)font->cell_h};
    SDL_FRect target = {(float)x, (float)y, (float)font->cell_w, (float)font->cell_h};
    SDL_SetTextureColorMod(font->atlas, color.r, color.g, color.b);
    SDL_RenderTexture(font->renderer, font->atlas, &source, &target);
}

void font_draw_text(const Font *font, int x, int y, const char *text, Color color) {
    for (int i = 0; text[i] != '\0'; i++) {
        font_draw_char(font, x + i * font->cell_w, y, text[i], color);
    }
}
