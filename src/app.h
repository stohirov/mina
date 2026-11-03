/* The application window: toolbar, folder tree, text area and status bar. All
 * paths are relative to the current working directory, which is the top of the
 * tree. */
#ifndef APP_H
#define APP_H

#include <SDL3/SDL.h>

#include "editor.h"
#include "font.h"
#include "tree.h"

typedef enum {
    MODE_EDIT,    /* Typing goes to the open file. */
    MODE_PROMPT,  /* Typing goes to the status bar, for a name. */
    MODE_CONFIRM  /* Waiting for y or n. */
} Mode;

typedef enum {
    ACTION_NONE,
    ACTION_NEW_FILE,
    ACTION_NEW_FOLDER,
    ACTION_RENAME,
    ACTION_DELETE,
    ACTION_SAVE_CHANGES
} Action;

typedef enum {
    BUTTON_NEW_FILE,
    BUTTON_NEW_FOLDER,
    BUTTON_RENAME,
    BUTTON_DELETE,
    BUTTON_SAVE,
    BUTTON_THEME,
    BUTTON_COUNT
} Button;

typedef struct {
    SDL_Renderer *renderer;
    Font font;
    int running;

    Tree tree;
    int tree_top;
    char *selected; /* Path of the highlighted tree row, or NULL. */

    Editor editor;
    int has_file;
    int dragging; /* 1 while the mouse is selecting text. */

    Mode mode;
    Action action;
    char input[256];
    char question[512];
    char *pending_open; /* File to open after answering about unsaved changes. */
    int pending_quit;   /* Quit after answering about unsaved changes. */

    char message[512];
} App;

/* Returns -1 and sets the SDL error if the font cannot be loaded. */
int app_init(App *app, SDL_Renderer *renderer, int font_size);
void app_free(App *app);

/* Mouse coordinates in events must already be in render coordinates. */
void app_handle_event(App *app, const SDL_Event *event);
void app_draw(App *app);

void app_open_file(App *app, const char *path);

SDL_Rect app_button_rect(const App *app, Button button);
SDL_Rect app_tree_rect(const App *app);
SDL_Rect app_editor_rect(const App *app);

#endif
