/* The application window: folder tree, tabs, text area and status bar. All paths
 * are relative to the current working directory, which is the top of the tree. */
#ifndef APP_H
#define APP_H

#include <SDL3/SDL.h>

#include "editor.h"
#include "font.h"
#include "tree.h"

typedef enum {
    MODE_EDIT,    /* Typing goes to the open file. */
    MODE_PROMPT,  /* Typing goes to the status bar, for a name. */
    MODE_CONFIRM, /* Waiting for y or n. */
    MODE_MENU     /* The right-click menu is open. */
} Mode;

typedef enum {
    ACTION_NONE,
    ACTION_NEW_FILE,
    ACTION_NEW_FOLDER,
    ACTION_RENAME,
    ACTION_DELETE,
    ACTION_CLOSE_TAB
} Action;

typedef enum {
    BUTTON_OPEN_FOLDER,
    BUTTON_SAVE,
    BUTTON_THEME,
    BUTTON_COUNT
} Button;

typedef enum {
    MENU_NEW_FILE,
    MENU_NEW_FOLDER,
    MENU_RENAME,
    MENU_DELETE,
    MENU_ITEM_COUNT
} MenuItem;

typedef struct {
    SDL_Renderer *renderer;
    Font font;
    int font_size;
    int running;

    Tree tree;
    int tree_top;
    int tree_left;  /* Columns the tree is scrolled sideways. */
    char *selected; /* Path of the highlighted tree row, or NULL. */

    Editor *tabs;   /* One open file per tab. */
    int tab_count;
    int active;     /* Index of the visible tab. */
    int first_tab;  /* Leftmost tab on screen, when they do not all fit. */
    int dragging;   /* 1 while the mouse is selecting text. */

    Mode mode;
    Action action;
    char input[256];
    char question[512];
    int pending_quit; /* Quit once every unsaved tab has been answered for. */
    char *pending_folder; /* The same, to open this folder. */
    int wants_folder_dialog; /* The main loop should show the folder picker. */

    MenuItem menu_items[MENU_ITEM_COUNT];
    int menu_count;
    int menu_x, menu_y;
    int menu_hover; /* Index of the item under the mouse, or -1. */

    char message[512];
} App;

/* Returns -1 and sets the SDL error if the font cannot be loaded. */
int app_init(App *app, SDL_Renderer *renderer, int font_size);
void app_free(App *app);

/* Mouse coordinates in events must already be in render coordinates. */
void app_handle_event(App *app, const SDL_Event *event);
void app_draw(App *app);

/* Change the text size, in pixels. Screens differ in how many pixels they have
 * per point, so this is needed when the window moves to another one. */
void app_set_font_size(App *app, int font_size);

/* Switch to the file's tab, or open it in a new one. */
void app_open_file(App *app, const char *path);

/* Make a folder the top of the tree, closing all tabs. Asks first about any
 * unsaved tab, so the switch may happen only after later events. */
void app_open_folder(App *app, const char *path);

/* Show a message in the status bar until the next key press or click. */
void app_show_message(App *app, const char *message);

SDL_Rect app_button_rect(const App *app, Button button);
SDL_Rect app_tree_rect(const App *app);
SDL_Rect app_editor_rect(const App *app);
SDL_Rect app_tab_rect(const App *app, int tab);
SDL_Rect app_tab_close_rect(const App *app, int tab);
SDL_Rect app_menu_item_rect(const App *app, int index);

#endif
