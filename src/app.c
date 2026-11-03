#include "app.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"
#include "theme.h"
#include "util.h"

#define SIDEBAR_COLS 30
#define WHEEL_LINES 3
#define MAX_TAB_NAME 24
#define MENU_COLS 12

static const char *const button_labels[BUTTON_COUNT] = {"Save", "Theme"};

static const char *const menu_labels[MENU_ITEM_COUNT] = {
    "New File", "New Folder", "Rename", "Delete",
};

/* ---------- Layout ---------- */

static void output_size(const App *app, int *w, int *h) {
    SDL_GetCurrentRenderOutputSize(app->renderer, w, h);
}

static int bar_height(const App *app) {
    return app->font.cell_h + app->font.cell_w;
}

static int sidebar_width(const App *app) {
    return app->font.cell_w * SIDEBAR_COLS;
}

/* The buttons sit in the top right corner, with Theme last. */
SDL_Rect app_button_rect(const App *app, Button button) {
    int w, h;
    output_size(app, &w, &h);
    int gap = app->font.cell_w / 2;
    int x = w;
    for (int i = BUTTON_COUNT - 1; i >= (int)button; i--) {
        x -= ((int)strlen(button_labels[i]) + 2) * app->font.cell_w + gap;
    }
    int width = ((int)strlen(button_labels[button]) + 2) * app->font.cell_w;
    return (SDL_Rect){x, gap / 2, width, bar_height(app) - gap};
}

SDL_Rect app_tree_rect(const App *app) {
    int w, h;
    output_size(app, &w, &h);
    return (SDL_Rect){0, 0, sidebar_width(app), SDL_max(h - bar_height(app), 1)};
}

SDL_Rect app_editor_rect(const App *app) {
    int w, h;
    output_size(app, &w, &h);
    int bar = bar_height(app);
    int x = sidebar_width(app) + app->font.cell_w;
    return (SDL_Rect){x, bar, SDL_max(w - x, 1), SDL_max(h - 2 * bar, 1)};
}

static int editor_rows(const App *app) {
    return SDL_max(app_editor_rect(app).h / app->font.cell_h, 1);
}

static int editor_cols(const App *app) {
    return SDL_max(app_editor_rect(app).w / app->font.cell_w, 1);
}

/* The space for tabs: from the sidebar to the first button. */
static SDL_Rect tab_strip_rect(const App *app) {
    int x = sidebar_width(app);
    int right = app_button_rect(app, BUTTON_SAVE).x;
    return (SDL_Rect){x, 0, SDL_max(right - x, 1), bar_height(app)};
}

static const char *tab_name(const Editor *tab) {
    const char *slash = strrchr(tab->path, '/');
    return slash != NULL ? slash + 1 : tab->path;
}

static int tab_name_len(const Editor *tab) {
    return SDL_min((int)strlen(tab_name(tab)), MAX_TAB_NAME);
}

/* Tabs before first_tab are off screen, and have no width. */
SDL_Rect app_tab_rect(const App *app, int tab) {
    int gap = app->font.cell_w / 2;
    int x = sidebar_width(app) + gap;
    for (int i = app->first_tab; i < tab; i++) {
        x += (tab_name_len(&app->tabs[i]) + 5) * app->font.cell_w + gap;
    }
    int width = tab < app->first_tab ? 0 : (tab_name_len(&app->tabs[tab]) + 5) * app->font.cell_w;
    return (SDL_Rect){x, gap, width, bar_height(app) - gap};
}

SDL_Rect app_tab_close_rect(const App *app, int tab) {
    SDL_Rect r = app_tab_rect(app, tab);
    int width = SDL_min(app->font.cell_w * 2, r.w);
    return (SDL_Rect){r.x + r.w - width, r.y, width, r.h};
}

SDL_Rect app_menu_item_rect(const App *app, int index) {
    int height = app->font.cell_h + app->font.cell_w / 2;
    return (SDL_Rect){app->menu_x, app->menu_y + index * height, app->font.cell_w * MENU_COLS, height};
}

static int point_in(const SDL_Rect *r, int x, int y) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* ---------- Tabs ---------- */

static Editor *current_tab(App *app) {
    return app->tab_count > 0 ? &app->tabs[app->active] : NULL;
}

static void set_message(App *app, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(app->message, sizeof(app->message), format, args);
    va_end(args);
}

static void set_selected(App *app, const char *path) {
    char *copy = path != NULL ? xstrdup(path) : NULL;
    free(app->selected);
    app->selected = copy;
}

static void refresh_tree(App *app) {
    if (tree_refresh(&app->tree) != 0) {
        set_message(app, "Cannot list folder: %s", strerror(errno));
    }
    if (app->tree_top >= app->tree.count) app->tree_top = SDL_max(app->tree.count - 1, 0);
}

static void activate_tab(App *app, int tab) {
    app->active = tab;
    app->dragging = 0;
    set_selected(app, app->tabs[tab].path);

    /* Scroll the tabs just enough to show this one. */
    int right = tab_strip_rect(app).x + tab_strip_rect(app).w;
    if (tab < app->first_tab) app->first_tab = tab;
    while (app->first_tab < tab) {
        SDL_Rect r = app_tab_rect(app, tab);
        if (r.x + r.w <= right) break;
        app->first_tab++;
    }
}

static void remove_tab(App *app, int tab) {
    editor_close(&app->tabs[tab]);
    memmove(&app->tabs[tab], &app->tabs[tab + 1], sizeof(Editor) * (size_t)(app->tab_count - tab - 1));
    app->tab_count--;

    if (tab < app->active || app->active >= app->tab_count) app->active--;
    if (app->active < 0) app->active = 0;
    if (app->first_tab >= app->tab_count) app->first_tab = SDL_max(app->tab_count - 1, 0);
    app->dragging = 0;
}

void app_open_file(App *app, const char *path) {
    for (int i = 0; i < app->tab_count; i++) {
        if (strcmp(app->tabs[i].path, path) == 0) {
            activate_tab(app, i);
            return;
        }
    }

    Editor next;
    if (editor_open(&next, path) != 0) {
        set_message(app, "Cannot open %s: %s", path, strerror(errno));
        return;
    }
    app->tabs = xrealloc(app->tabs, sizeof(Editor) * ((size_t)app->tab_count + 1));
    app->tabs[app->tab_count++] = next;
    activate_tab(app, app->tab_count - 1);
}

/* ---------- Questions ---------- */

static void ask_confirm(App *app, Action action, const char *question) {
    app->mode = MODE_CONFIRM;
    app->action = action;
    snprintf(app->question, sizeof(app->question), "%s", question);
}

static void ask_name(App *app, Action action, const char *question, const char *initial) {
    app->mode = MODE_PROMPT;
    app->action = action;
    snprintf(app->question, sizeof(app->question), "%s", question);
    snprintf(app->input, sizeof(app->input), "%s", initial);
}

static void back_to_editing(App *app) {
    app->mode = MODE_EDIT;
    app->action = ACTION_NONE;
    app->pending_quit = 0;
}

static void ask_about_unsaved_changes(App *app) {
    char question[512];
    snprintf(question, sizeof(question), "Save changes to %s? y = save, n = discard, Esc = cancel",
             app->tabs[app->active].path);
    ask_confirm(app, ACTION_CLOSE_TAB, question);
}

static void close_tab(App *app, int tab) {
    if (app->tabs[tab].buffer.dirty) {
        activate_tab(app, tab);
        ask_about_unsaved_changes(app);
        return;
    }
    int was_active = tab == app->active;
    remove_tab(app, tab);
    if (was_active && app->tab_count > 0) activate_tab(app, app->active);
}

/* Ask about each unsaved tab in turn, then stop. */
static void request_quit(App *app) {
    back_to_editing(app);
    for (int i = 0; i < app->tab_count; i++) {
        if (app->tabs[i].buffer.dirty) {
            app->pending_quit = 1;
            activate_tab(app, i);
            ask_about_unsaved_changes(app);
            return;
        }
    }
    app->running = 0;
}

static int save_file(App *app) {
    Editor *tab = current_tab(app);
    if (tab == NULL) {
        set_message(app, "No file is open");
        return -1;
    }
    if (editor_save(tab) != 0) {
        set_message(app, "Save failed: %s", strerror(errno));
        return -1;
    }
    set_message(app, "Saved %s", tab->path);
    return 0;
}

/* The unsaved-changes question is answered: save if asked, then close the tab. */
static void finish_closing_tab(App *app, int save) {
    if (save && save_file(app) != 0) {
        back_to_editing(app);
        return;
    }

    int quitting = app->pending_quit;
    back_to_editing(app);
    remove_tab(app, app->active);
    if (quitting) {
        request_quit(app);
    } else if (app->tab_count > 0) {
        activate_tab(app, app->active);
    }
}

/* ---------- File and folder actions ---------- */

/* Where a new item goes: the selected folder, or the selected file's folder. */
static char *target_folder(const App *app) {
    if (app->selected == NULL) return xstrdup(".");
    int index = tree_find(&app->tree, app->selected);
    if (index >= 0 && app->tree.rows[index].is_dir) return xstrdup(app->selected);
    return fs_parent(app->selected);
}

static void create_item(App *app, int is_folder) {
    char *folder = target_folder(app);
    char *path = fs_join(folder, app->input);

    int result = is_folder ? fs_create_folder(path) : fs_create_file(path);
    if (result != 0) {
        set_message(app, "Cannot create %s: %s", path, strerror(errno));
    } else {
        set_message(app, "Created %s", path);
        tree_set_expanded(&app->tree, folder, 1);
        refresh_tree(app);
        set_selected(app, path);
        if (!is_folder) app_open_file(app, path);
    }
    free(folder);
    free(path);
}

/* If a tab's file is at old_path or inside it, its path has changed. */
static void update_tab_paths(App *app, const char *old_path, const char *new_path) {
    size_t len = strlen(old_path);
    for (int i = 0; i < app->tab_count; i++) {
        const char *current = app->tabs[i].path;
        if (strncmp(current, old_path, len) != 0) continue;
        if (current[len] != '\0' && current[len] != '/') continue;

        char *updated = current[len] == '\0' ? xstrdup(new_path) : fs_join(new_path, current + len + 1);
        editor_set_path(&app->tabs[i], updated);
        free(updated);
    }
}

static void rename_selected(App *app) {
    char *parent = fs_parent(app->selected);
    char *path = fs_join(parent, app->input);

    if (fs_rename(app->selected, path) != 0) {
        set_message(app, "Cannot rename %s: %s", app->selected, strerror(errno));
    } else {
        set_message(app, "Renamed to %s", path);
        update_tab_paths(app, app->selected, path);
        set_selected(app, path);
        refresh_tree(app);
    }
    free(parent);
    free(path);
}

static void delete_selected(App *app) {
    if (fs_delete(app->selected) != 0) {
        set_message(app, "Cannot delete %s: %s", app->selected, strerror(errno));
    } else {
        set_message(app, "Deleted %s", app->selected);
        set_selected(app, NULL);
    }
    /* Close the tabs of whatever is gone. A folder may be only partly deleted. */
    for (int i = app->tab_count - 1; i >= 0; i--) {
        if (access(app->tabs[i].path, F_OK) != 0) remove_tab(app, i);
    }
    refresh_tree(app);
}

static void submit_name(App *app) {
    if (app->input[0] == '\0') return;
    Action action = app->action;
    back_to_editing(app);
    if (!fs_valid_name(app->input)) {
        set_message(app, "Invalid name: %s", app->input);
        return;
    }

    switch (action) {
        case ACTION_NEW_FILE: create_item(app, 0); break;
        case ACTION_NEW_FOLDER: create_item(app, 1); break;
        case ACTION_RENAME: rename_selected(app); break;
        default: break;
    }
}

/* ---------- Right-click menu ---------- */

static void close_menu(App *app) {
    app->mode = MODE_EDIT;
    app->menu_count = 0;
}

/* A row offers everything. Empty space has nothing to rename or delete. */
static void open_menu(App *app, int x, int y, int on_row) {
    app->menu_count = 0;
    app->menu_items[app->menu_count++] = MENU_NEW_FILE;
    app->menu_items[app->menu_count++] = MENU_NEW_FOLDER;
    if (on_row) {
        app->menu_items[app->menu_count++] = MENU_RENAME;
        app->menu_items[app->menu_count++] = MENU_DELETE;
    }

    /* Keep the whole menu inside the window. */
    int w, h;
    output_size(app, &w, &h);
    app->menu_x = x;
    app->menu_y = y;
    SDL_Rect last = app_menu_item_rect(app, app->menu_count - 1);
    if (last.x + last.w > w) app->menu_x = SDL_max(w - last.w, 0);
    if (last.y + last.h > h) app->menu_y = SDL_max(h - app->menu_count * last.h, 0);

    app->menu_hover = -1;
    app->mode = MODE_MENU;
}

static int menu_item_at(const App *app, int x, int y) {
    for (int i = 0; i < app->menu_count; i++) {
        SDL_Rect r = app_menu_item_rect(app, i);
        if (point_in(&r, x, y)) return i;
    }
    return -1;
}

static void run_menu_item(App *app, MenuItem item) {
    char question[512];
    int index = app->selected != NULL ? tree_find(&app->tree, app->selected) : -1;

    switch (item) {
        case MENU_NEW_FILE:
            ask_name(app, ACTION_NEW_FILE, "New file name", "");
            break;
        case MENU_NEW_FOLDER:
            ask_name(app, ACTION_NEW_FOLDER, "New folder name", "");
            break;
        case MENU_RENAME:
            if (index >= 0) {
                snprintf(question, sizeof(question), "Rename %s to", app->selected);
                ask_name(app, ACTION_RENAME, question, app->tree.rows[index].name);
            }
            break;
        case MENU_DELETE:
            if (index >= 0) {
                snprintf(question, sizeof(question),
                         app->tree.rows[index].is_dir
                             ? "Delete folder %s and everything in it? y = delete, n = cancel"
                             : "Delete file %s? y = delete, n = cancel",
                         app->selected);
                ask_confirm(app, ACTION_DELETE, question);
            }
            break;
        case MENU_ITEM_COUNT:
            break;
    }
}

/* ---------- Input ---------- */

static void copy_selection(App *app) {
    char *text = editor_selected_text(current_tab(app));
    if (text == NULL) {
        set_message(app, "Select some text first");
        return;
    }
    if (!SDL_SetClipboardText(text)) {
        set_message(app, "Cannot copy: %s", SDL_GetError());
    } else {
        set_message(app, "Copied");
    }
    free(text);
}

static void paste(App *app) {
    char *text = SDL_GetClipboardText();
    if (text == NULL) {
        set_message(app, "Cannot paste: %s", SDL_GetError());
        return;
    }
    Editor *tab = current_tab(app);
    editor_insert_text(tab, text, strlen(text));
    SDL_free(text);
}

static void undo(App *app) {
    if (!editor_undo(current_tab(app))) set_message(app, "Nothing to undo");
}

static void on_edit_key(App *app, const SDL_KeyboardEvent *key) {
    int command = (key->mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;
    int shift = (key->mod & SDL_KMOD_SHIFT) != 0;

    if (command && key->key == SDLK_Q) {
        request_quit(app);
        return;
    }

    Editor *e = current_tab(app);
    if (e == NULL) return;

    if (command && key->key == SDLK_S) {
        save_file(app);
        return;
    }
    if (command && key->key == SDLK_C) {
        copy_selection(app);
        return;
    }
    if (command && key->key == SDLK_W) {
        close_tab(app, app->active);
        return;
    }
    if ((key->mod & SDL_KMOD_CTRL) != 0 && key->key == SDLK_TAB) {
        int step = shift ? app->tab_count - 1 : 1;
        activate_tab(app, (app->active + step) % app->tab_count);
        return;
    }

    int page = editor_rows(app);
    if (command && key->key == SDLK_V) {
        paste(app);
    } else if (command && key->key == SDLK_Z) {
        if (shift) return;
        undo(app);
    } else {
        switch (key->key) {
            case SDLK_RETURN: case SDLK_KP_ENTER: editor_insert_newline(e); break;
            case SDLK_BACKSPACE: editor_backspace(e); break;
            case SDLK_DELETE: editor_delete_forward(e); break;
            case SDLK_TAB: editor_insert_char(e, '\t'); break;
            case SDLK_LEFT: editor_move(e, command ? MOVE_LINE_START : MOVE_LEFT, page, shift); break;
            case SDLK_RIGHT: editor_move(e, command ? MOVE_LINE_END : MOVE_RIGHT, page, shift); break;
            case SDLK_UP: editor_move(e, MOVE_UP, page, shift); break;
            case SDLK_DOWN: editor_move(e, MOVE_DOWN, page, shift); break;
            case SDLK_HOME: editor_move(e, MOVE_LINE_START, page, shift); break;
            case SDLK_END: editor_move(e, MOVE_LINE_END, page, shift); break;
            case SDLK_PAGEUP: editor_move(e, MOVE_PAGE_UP, page, shift); break;
            case SDLK_PAGEDOWN: editor_move(e, MOVE_PAGE_DOWN, page, shift); break;
            default: return;
        }
    }
    editor_keep_cursor_visible(e, page, editor_cols(app));
}

static void on_prompt_key(App *app, const SDL_KeyboardEvent *key) {
    size_t len = strlen(app->input);
    switch (key->key) {
        case SDLK_ESCAPE: back_to_editing(app); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: submit_name(app); break;
        case SDLK_BACKSPACE:
            if (len > 0) app->input[len - 1] = '\0';
            break;
        default: break;
    }
}

static void on_key(App *app, const SDL_KeyboardEvent *key) {
    switch (app->mode) {
        case MODE_EDIT: on_edit_key(app, key); break;
        case MODE_PROMPT: on_prompt_key(app, key); break;
        case MODE_CONFIRM:
            if (key->key == SDLK_ESCAPE) back_to_editing(app);
            break;
        case MODE_MENU:
            if (key->key == SDLK_ESCAPE) close_menu(app);
            break;
    }
}

static void on_confirm_answer(App *app, char answer) {
    if (answer != 'y' && answer != 'Y' && answer != 'n' && answer != 'N') return;
    int yes = answer == 'y' || answer == 'Y';

    if (app->action == ACTION_DELETE) {
        back_to_editing(app);
        if (yes) delete_selected(app);
    } else if (app->action == ACTION_CLOSE_TAB) {
        finish_closing_tab(app, yes);
    }
}

/* The characters of a key press arrive here, after its KEY_DOWN event. So a
 * y or n answer must be read here too: if KEY_DOWN handled it, the character
 * that follows would be typed into the file. */
static void on_text(App *app, const char *text) {
    for (const char *p = text; *p != '\0'; p++) {
        char c = *p;
        if (c < 32 || c > 126) {
            if ((unsigned char)c >= 0x80) set_message(app, "Only English characters are supported");
            continue;
        }

        Editor *e = current_tab(app);
        switch (app->mode) {
            case MODE_EDIT:
                if (e != NULL) {
                    editor_insert_char(e, c);
                    editor_keep_cursor_visible(e, editor_rows(app), editor_cols(app));
                } else {
                    set_message(app, "Open a file to start typing");
                }
                break;
            case MODE_PROMPT: {
                size_t len = strlen(app->input);
                if (len + 1 < sizeof(app->input)) {
                    app->input[len] = c;
                    app->input[len + 1] = '\0';
                }
                break;
            }
            case MODE_CONFIRM:
                on_confirm_answer(app, c);
                return;
            case MODE_MENU:
                return;
        }
    }
}

static void on_tree_click(App *app, int y) {
    SDL_Rect rect = app_tree_rect(app);
    int index = app->tree_top + (y - rect.y) / app->font.cell_h;
    if (index >= app->tree.count) {
        set_selected(app, NULL);
        return;
    }

    const TreeRow *row = &app->tree.rows[index];
    set_selected(app, row->path);
    if (!row->is_dir) {
        app_open_file(app, app->selected);
    } else if (row->expanded) {
        tree_set_expanded(&app->tree, app->selected, 0);
    } else if (access(row->path, R_OK | X_OK) != 0) {
        set_message(app, "Cannot open %s: %s", row->path, strerror(errno));
    } else {
        tree_set_expanded(&app->tree, app->selected, 1);
    }
}

/* Returns 1 if the click landed on a tab. */
static int on_tab_click(App *app, int x, int y) {
    SDL_Rect strip = tab_strip_rect(app);
    if (!point_in(&strip, x, y)) return 0;

    for (int i = app->first_tab; i < app->tab_count; i++) {
        SDL_Rect tab = app_tab_rect(app, i);
        if (!point_in(&tab, x, y)) continue;

        SDL_Rect close = app_tab_close_rect(app, i);
        if (point_in(&close, x, y)) {
            close_tab(app, i);
        } else {
            activate_tab(app, i);
        }
        return 1;
    }
    return 1;
}

/* Put the cursor under the mouse. Above the text area means the row above the
 * first visible one, so that dragging upwards can scroll. */
static void place_cursor(App *app, int x, int y, int extend) {
    SDL_Rect text = app_editor_rect(app);
    Editor *e = current_tab(app);
    int row = y < text.y ? e->top - 1 : e->top + (y - text.y) / app->font.cell_h;
    int col = x < text.x ? e->left - 1 : e->left + (x - text.x) / app->font.cell_w;
    editor_click(e, row, col, extend);
}

static void on_click(App *app, int x, int y) {
    if (app->mode == MODE_MENU) {
        int index = menu_item_at(app, x, y);
        MenuItem item = index >= 0 ? app->menu_items[index] : MENU_ITEM_COUNT;
        close_menu(app);
        run_menu_item(app, item);
        return;
    }
    /* A question is waiting: only y, n or Esc may answer it. */
    if (app->mode != MODE_EDIT) return;

    SDL_Rect save = app_button_rect(app, BUTTON_SAVE);
    if (point_in(&save, x, y)) {
        save_file(app);
        return;
    }
    SDL_Rect theme = app_button_rect(app, BUTTON_THEME);
    if (point_in(&theme, x, y)) {
        theme_toggle();
        return;
    }

    SDL_Rect tree = app_tree_rect(app);
    if (point_in(&tree, x, y)) {
        on_tree_click(app, y);
        return;
    }
    if (on_tab_click(app, x, y)) return;

    SDL_Rect text = app_editor_rect(app);
    if (current_tab(app) != NULL && point_in(&text, x, y)) {
        int shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        place_cursor(app, x, y, shift);
        app->dragging = 1;
    }
}

static void on_right_click(App *app, int x, int y) {
    if (app->mode == MODE_MENU) close_menu(app);
    if (app->mode != MODE_EDIT) return;

    SDL_Rect tree = app_tree_rect(app);
    if (!point_in(&tree, x, y)) return;

    int index = app->tree_top + (y - tree.y) / app->font.cell_h;
    int on_row = index < app->tree.count;
    set_selected(app, on_row ? app->tree.rows[index].path : NULL);
    open_menu(app, x, y, on_row);
}

static void on_mouse_motion(App *app, const SDL_MouseMotionEvent *motion) {
    if (app->mode == MODE_MENU) {
        app->menu_hover = menu_item_at(app, (int)motion->x, (int)motion->y);
        return;
    }
    if (!app->dragging) return;
    if ((motion->state & SDL_BUTTON_LMASK) == 0) {
        app->dragging = 0;
        return;
    }
    place_cursor(app, (int)motion->x, (int)motion->y, 1);
    editor_keep_cursor_visible(current_tab(app), editor_rows(app), editor_cols(app));
}

static void on_wheel(App *app, const SDL_MouseWheelEvent *wheel) {
    int amount = wheel->integer_y * WHEEL_LINES;
    int x = (int)wheel->mouse_x;
    int y = (int)wheel->mouse_y;
    SDL_Rect tree = app_tree_rect(app);
    SDL_Rect strip = tab_strip_rect(app);
    Editor *e = current_tab(app);

    if (point_in(&tree, x, y)) {
        int top = app->tree_top - amount;
        app->tree_top = SDL_clamp(top, 0, SDL_max(app->tree.count - 1, 0));
    } else if (point_in(&strip, x, y)) {
        int first = app->first_tab - wheel->integer_y + wheel->integer_x;
        app->first_tab = SDL_clamp(first, 0, SDL_max(app->tab_count - 1, 0));
    } else if (e != NULL) {
        e->top = SDL_clamp(e->top - amount, 0, e->buffer.count - 1);
        e->left = SDL_max(e->left + wheel->integer_x * WHEEL_LINES, 0);
    }
}

void app_handle_event(App *app, const SDL_Event *event) {
    switch (event->type) {
        case SDL_EVENT_QUIT:
            request_quit(app);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            refresh_tree(app);
            break;
        case SDL_EVENT_KEY_DOWN:
            app->message[0] = '\0';
            on_key(app, &event->key);
            break;
        case SDL_EVENT_TEXT_INPUT:
            on_text(app, event->text.text);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            app->message[0] = '\0';
            if (event->button.button == SDL_BUTTON_LEFT) {
                on_click(app, (int)event->button.x, (int)event->button.y);
            } else if (event->button.button == SDL_BUTTON_RIGHT) {
                on_right_click(app, (int)event->button.x, (int)event->button.y);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button == SDL_BUTTON_LEFT) app->dragging = 0;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            on_mouse_motion(app, &event->motion);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            on_wheel(app, &event->wheel);
            break;
        default:
            break;
    }
}

/* ---------- Drawing ---------- */

static void fill_rect(const App *app, int x, int y, int w, int h, Color color) {
    SDL_FRect rect = {(float)x, (float)y, (float)w, (float)h};
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, 255);
    SDL_RenderFillRect(app->renderer, &rect);
}

static void draw_tabs(const App *app) {
    const Theme *theme = theme_current();
    SDL_Rect strip = tab_strip_rect(app);
    int cell = app->font.cell_w;
    SDL_SetRenderClipRect(app->renderer, &strip);

    for (int i = app->first_tab; i < app->tab_count; i++) {
        const Editor *tab = &app->tabs[i];
        SDL_Rect r = app_tab_rect(app, i);
        if (r.x >= strip.x + strip.w) break;

        fill_rect(app, r.x, r.y, r.w, r.h, i == app->active ? theme->background : theme->button);

        char name[MAX_TAB_NAME + 1];
        snprintf(name, sizeof(name), "%s", tab_name(tab));
        int text_y = r.y + (r.h - app->font.cell_h) / 2;
        font_draw_text(&app->font, r.x + cell, text_y, name, theme->text);
        if (tab->buffer.dirty) {
            font_draw_char(&app->font, r.x + cell * (tab_name_len(tab) + 1), text_y, '*', theme->text);
        }
        SDL_Rect close = app_tab_close_rect(app, i);
        font_draw_char(&app->font, close.x, text_y, 'x', theme->text);
    }
    SDL_SetRenderClipRect(app->renderer, NULL);
}

static void draw_top_bar(const App *app) {
    const Theme *theme = theme_current();
    int w, h;
    output_size(app, &w, &h);
    int x = sidebar_width(app);
    fill_rect(app, x, 0, w - x, bar_height(app), theme->bar);

    draw_tabs(app);

    for (int i = 0; i < BUTTON_COUNT; i++) {
        SDL_Rect r = app_button_rect(app, (Button)i);
        fill_rect(app, r.x, r.y, r.w, r.h, theme->button);
        font_draw_text(&app->font, r.x + app->font.cell_w, r.y + (r.h - app->font.cell_h) / 2,
                       button_labels[i], theme->text);
    }
}

static void draw_tree(const App *app) {
    const Theme *theme = theme_current();
    SDL_Rect rect = app_tree_rect(app);
    fill_rect(app, rect.x, rect.y, rect.w, rect.h, theme->sidebar);
    SDL_SetRenderClipRect(app->renderer, &rect);

    int rows = rect.h / app->font.cell_h + 1;
    for (int i = 0; i < rows && app->tree_top + i < app->tree.count; i++) {
        const TreeRow *row = &app->tree.rows[app->tree_top + i];
        int y = rect.y + i * app->font.cell_h;

        if (app->selected != NULL && strcmp(row->path, app->selected) == 0) {
            fill_rect(app, rect.x, y, rect.w, app->font.cell_h, theme->selected);
        }

        int x = rect.x + app->font.cell_w * (row->depth * 2 + 1);
        if (row->is_dir) {
            font_draw_char(&app->font, x, y, row->expanded ? 'v' : '>', theme->text);
        }
        font_draw_text(&app->font, x + app->font.cell_w * 2, y, row->name, theme->text);
    }
    SDL_SetRenderClipRect(app->renderer, NULL);
}

static void draw_line(const App *app, const Line *line, int y, int left, int cols) {
    const SDL_Rect rect = app_editor_rect(app);
    int x = 0;
    for (int i = 0; i < line->len && x < left + cols; i++) {
        char c = line->text[i];
        if (c == '\t') {
            x += TAB_WIDTH - x % TAB_WIDTH;
        } else {
            if (x >= left) {
                font_draw_char(&app->font, rect.x + (x - left) * app->font.cell_w, y, c,
                               theme_current()->text);
            }
            x++;
        }
    }
}

/* Highlight the selected part of a visible row. A selected line break shows as
 * one extra column. */
static void draw_selection(const App *app, const Editor *e, int row, int y) {
    Pos start, end;
    if (!editor_selection(e, &start, &end) || row < start.row || row > end.row) return;

    int from = row == start.row ? editor_screen_col(e, row, start.col) : 0;
    int to = row == end.row ? editor_screen_col(e, row, end.col)
                            : editor_screen_col(e, row, e->buffer.lines[row].len) + 1;
    SDL_Rect rect = app_editor_rect(app);
    fill_rect(app, rect.x + (from - e->left) * app->font.cell_w, y,
              (to - from) * app->font.cell_w, app->font.cell_h, theme_current()->selected);
}

static void draw_editor(App *app) {
    const Theme *theme = theme_current();
    SDL_Rect rect = app_editor_rect(app);
    SDL_SetRenderClipRect(app->renderer, &rect);

    const Editor *e = current_tab(app);
    if (e == NULL) {
        font_draw_text(&app->font, rect.x, rect.y, "Click a file in the tree to open it.", theme->text);
        font_draw_text(&app->font, rect.x, rect.y + app->font.cell_h,
                       "Right-click the tree to create, rename or delete.", theme->text);
    } else {
        int rows = editor_rows(app);
        int cols = editor_cols(app);
        for (int i = 0; i < rows && e->top + i < e->buffer.count; i++) {
            int y = rect.y + i * app->font.cell_h;
            draw_selection(app, e, e->top + i, y);
            draw_line(app, &e->buffer.lines[e->top + i], y, e->left, cols);
        }

        int screen_row = e->row - e->top;
        int screen_col = editor_screen_col(e, e->row, e->col) - e->left;
        if (screen_row >= 0 && screen_row < rows && screen_col >= 0 && screen_col < cols) {
            fill_rect(app, rect.x + screen_col * app->font.cell_w,
                      rect.y + screen_row * app->font.cell_h,
                      SDL_max(app->font.cell_w / 5, 1), app->font.cell_h, theme->cursor);
        }
    }
    SDL_SetRenderClipRect(app->renderer, NULL);
}

static void draw_status(App *app) {
    const Theme *theme = theme_current();
    int w, h;
    output_size(app, &w, &h);
    int bar = bar_height(app);
    fill_rect(app, 0, h - bar, w, bar, theme->bar);

    char text[1024];
    int show_cursor = 0;
    const Editor *e = current_tab(app);
    if (app->mode == MODE_PROMPT) {
        snprintf(text, sizeof(text), "%s: %s", app->question, app->input);
        show_cursor = 1;
    } else if (app->mode == MODE_CONFIRM) {
        snprintf(text, sizeof(text), "%s", app->question);
    } else if (app->message[0] != '\0') {
        snprintf(text, sizeof(text), "%s", app->message);
    } else if (e != NULL) {
        snprintf(text, sizeof(text), "%s%s | Ln %d/%d, Col %d | %s theme", e->path,
                 e->buffer.dirty ? " (modified)" : "", e->row + 1, e->buffer.count, e->col + 1,
                 theme->name);
    } else {
        snprintf(text, sizeof(text), "No file open | %s theme", theme->name);
    }

    /* If it does not fit, show the end: that is where the typing happens. */
    int max_chars = SDL_max(w / app->font.cell_w - 2, 1);
    int len = (int)strlen(text);
    int start = (show_cursor && len > max_chars) ? len - max_chars : 0;
    int x = app->font.cell_w;
    int y = h - bar + app->font.cell_w / 2;
    font_draw_text(&app->font, x, y, text + start, theme->text);
    if (show_cursor) {
        fill_rect(app, x + (len - start) * app->font.cell_w, y,
                  SDL_max(app->font.cell_w / 5, 1), app->font.cell_h, theme->cursor);
    }
}

static void draw_menu(const App *app) {
    const Theme *theme = theme_current();
    SDL_Rect first = app_menu_item_rect(app, 0);
    int border = SDL_max(app->font.cell_w / 8, 1);
    fill_rect(app, first.x - border, first.y - border, first.w + 2 * border,
              first.h * app->menu_count + 2 * border, theme->button);

    for (int i = 0; i < app->menu_count; i++) {
        SDL_Rect r = app_menu_item_rect(app, i);
        fill_rect(app, r.x, r.y, r.w, r.h, i == app->menu_hover ? theme->selected : theme->bar);
        font_draw_text(&app->font, r.x + app->font.cell_w, r.y + (r.h - app->font.cell_h) / 2,
                       menu_labels[app->menu_items[i]], theme->text);
    }
}

void app_draw(App *app) {
    Color background = theme_current()->background;
    SDL_SetRenderDrawColor(app->renderer, background.r, background.g, background.b, 255);
    SDL_RenderClear(app->renderer);

    draw_editor(app);
    draw_tree(app);
    draw_top_bar(app);
    draw_status(app);
    if (app->mode == MODE_MENU) draw_menu(app);
}

/* ---------- Lifetime ---------- */

int app_init(App *app, SDL_Renderer *renderer, int font_size) {
    memset(app, 0, sizeof(*app));
    app->renderer = renderer;
    app->running = 1;
    if (font_load(&app->font, renderer, font_size) != 0) return -1;
    refresh_tree(app);
    return 0;
}

void app_free(App *app) {
    for (int i = 0; i < app->tab_count; i++) {
        editor_close(&app->tabs[i]);
    }
    free(app->tabs);
    app->tabs = NULL;
    app->tab_count = 0;
    tree_free(&app->tree);
    font_free(&app->font);
    free(app->selected);
    app->selected = NULL;
}
