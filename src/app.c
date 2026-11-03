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

static const char *const button_labels[BUTTON_COUNT] = {
    "New File", "New Folder", "Rename", "Delete", "Save", "Theme",
};

/* ---------- Layout ---------- */

static void output_size(const App *app, int *w, int *h) {
    SDL_GetCurrentRenderOutputSize(app->renderer, w, h);
}

static int bar_height(const App *app) {
    return app->font.cell_h + app->font.cell_w;
}

SDL_Rect app_button_rect(const App *app, Button button) {
    int gap = app->font.cell_w / 2;
    int x = gap;
    for (int i = 0; i < (int)button; i++) {
        x += ((int)strlen(button_labels[i]) + 2) * app->font.cell_w + gap;
    }
    int width = ((int)strlen(button_labels[button]) + 2) * app->font.cell_w;
    return (SDL_Rect){x, gap / 2, width, bar_height(app) - gap};
}

SDL_Rect app_tree_rect(const App *app) {
    int w, h;
    output_size(app, &w, &h);
    int bar = bar_height(app);
    return (SDL_Rect){0, bar, app->font.cell_w * SIDEBAR_COLS, SDL_max(h - 2 * bar, 1)};
}

SDL_Rect app_editor_rect(const App *app) {
    int w, h;
    output_size(app, &w, &h);
    int bar = bar_height(app);
    int x = app->font.cell_w * (SIDEBAR_COLS + 1);
    return (SDL_Rect){x, bar, SDL_max(w - x, 1), SDL_max(h - 2 * bar, 1)};
}

static int editor_rows(const App *app) {
    return SDL_max(app_editor_rect(app).h / app->font.cell_h, 1);
}

static int editor_cols(const App *app) {
    return SDL_max(app_editor_rect(app).w / app->font.cell_w, 1);
}

static int point_in(const SDL_Rect *r, int x, int y) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* ---------- State helpers ---------- */

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

static void close_file(App *app) {
    if (app->has_file) editor_close(&app->editor);
    app->has_file = 0;
}

void app_open_file(App *app, const char *path) {
    Editor next;
    if (editor_open(&next, path) != 0) {
        set_message(app, "Cannot open %s: %s", path, strerror(errno));
        return;
    }
    close_file(app);
    app->editor = next;
    app->has_file = 1;
    set_selected(app, path);
}

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
    free(app->pending_open);
    app->pending_open = NULL;
    app->pending_quit = 0;
}

static void ask_about_unsaved_changes(App *app) {
    char question[512];
    snprintf(question, sizeof(question), "Save changes to %s? y = save, n = discard, Esc = cancel",
             app->editor.path);
    ask_confirm(app, ACTION_SAVE_CHANGES, question);
}

static int has_unsaved_changes(const App *app) {
    return app->has_file && app->editor.buffer.dirty;
}

static void request_open(App *app, const char *path) {
    if (app->has_file && strcmp(app->editor.path, path) == 0) return;
    if (has_unsaved_changes(app)) {
        free(app->pending_open);
        app->pending_open = xstrdup(path);
        ask_about_unsaved_changes(app);
    } else {
        app_open_file(app, path);
    }
}

static void request_quit(App *app) {
    back_to_editing(app);
    if (has_unsaved_changes(app)) {
        app->pending_quit = 1;
        ask_about_unsaved_changes(app);
    } else {
        app->running = 0;
    }
}

static int save_file(App *app) {
    if (editor_save(&app->editor) != 0) {
        set_message(app, "Save failed: %s", strerror(errno));
        return -1;
    }
    set_message(app, "Saved %s", app->editor.path);
    return 0;
}

/* The unsaved-changes question is answered: do what was waiting for it. */
static void continue_pending(App *app) {
    char *path = app->pending_open;
    int quit = app->pending_quit;
    app->pending_open = NULL;
    back_to_editing(app);

    if (quit) {
        app->running = 0;
    } else if (path != NULL) {
        app_open_file(app, path);
    }
    free(path);
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
        if (!is_folder) request_open(app, path);
    }
    free(folder);
    free(path);
}

/* If the open file is at old_path or inside it, its path has changed. */
static void update_open_path(App *app, const char *old_path, const char *new_path) {
    if (!app->has_file) return;

    size_t len = strlen(old_path);
    const char *current = app->editor.path;
    if (strncmp(current, old_path, len) != 0) return;
    if (current[len] != '\0' && current[len] != '/') return;

    char *updated = current[len] == '\0' ? xstrdup(new_path) : fs_join(new_path, current + len + 1);
    editor_set_path(&app->editor, updated);
    free(updated);
}

static void rename_selected(App *app) {
    char *parent = fs_parent(app->selected);
    char *path = fs_join(parent, app->input);

    if (fs_rename(app->selected, path) != 0) {
        set_message(app, "Cannot rename %s: %s", app->selected, strerror(errno));
    } else {
        set_message(app, "Renamed to %s", path);
        update_open_path(app, app->selected, path);
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
    if (app->has_file && access(app->editor.path, F_OK) != 0) close_file(app);
    /* Refresh even after a failure: a folder may be partly deleted. */
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

static void press_button(App *app, Button button) {
    char question[512];
    int index = app->selected != NULL ? tree_find(&app->tree, app->selected) : -1;

    switch (button) {
        case BUTTON_NEW_FILE:
            ask_name(app, ACTION_NEW_FILE, "New file name", "");
            break;
        case BUTTON_NEW_FOLDER:
            ask_name(app, ACTION_NEW_FOLDER, "New folder name", "");
            break;
        case BUTTON_RENAME:
            if (index < 0) {
                set_message(app, "Select a file or folder first");
            } else {
                snprintf(question, sizeof(question), "Rename %s to", app->selected);
                ask_name(app, ACTION_RENAME, question, app->tree.rows[index].name);
            }
            break;
        case BUTTON_DELETE:
            if (index < 0) {
                set_message(app, "Select a file or folder first");
            } else {
                snprintf(question, sizeof(question),
                         app->tree.rows[index].is_dir
                             ? "Delete folder %s and everything in it? y = delete, n = cancel"
                             : "Delete file %s? y = delete, n = cancel",
                         app->selected);
                ask_confirm(app, ACTION_DELETE, question);
            }
            break;
        case BUTTON_SAVE:
            if (app->has_file) {
                save_file(app);
            } else {
                set_message(app, "No file is open");
            }
            break;
        case BUTTON_THEME:
            theme_toggle();
            break;
        case BUTTON_COUNT:
            break;
    }
}

/* ---------- Input ---------- */

static void on_edit_key(App *app, const SDL_KeyboardEvent *key) {
    int command = (key->mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;

    if (command && key->key == SDLK_Q) {
        request_quit(app);
        return;
    }
    if (!app->has_file) return;
    if (command && key->key == SDLK_S) {
        save_file(app);
        return;
    }

    Editor *e = &app->editor;
    int page = editor_rows(app);
    switch (key->key) {
        case SDLK_RETURN: case SDLK_KP_ENTER: editor_insert_newline(e); break;
        case SDLK_BACKSPACE: editor_backspace(e); break;
        case SDLK_DELETE: editor_delete_forward(e); break;
        case SDLK_TAB: editor_insert_char(e, '\t'); break;
        case SDLK_LEFT: editor_move(e, command ? MOVE_LINE_START : MOVE_LEFT, page); break;
        case SDLK_RIGHT: editor_move(e, command ? MOVE_LINE_END : MOVE_RIGHT, page); break;
        case SDLK_UP: editor_move(e, MOVE_UP, page); break;
        case SDLK_DOWN: editor_move(e, MOVE_DOWN, page); break;
        case SDLK_HOME: editor_move(e, MOVE_LINE_START, page); break;
        case SDLK_END: editor_move(e, MOVE_LINE_END, page); break;
        case SDLK_PAGEUP: editor_move(e, MOVE_PAGE_UP, page); break;
        case SDLK_PAGEDOWN: editor_move(e, MOVE_PAGE_DOWN, page); break;
        default: return;
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
    }
}

static void on_confirm_answer(App *app, char answer) {
    if (answer != 'y' && answer != 'Y' && answer != 'n' && answer != 'N') return;
    int yes = answer == 'y' || answer == 'Y';

    if (app->action == ACTION_DELETE) {
        back_to_editing(app);
        if (yes) delete_selected(app);
    } else if (app->action == ACTION_SAVE_CHANGES) {
        if (yes && save_file(app) != 0) {
            back_to_editing(app);
        } else {
            continue_pending(app);
        }
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

        switch (app->mode) {
            case MODE_EDIT:
                if (app->has_file) {
                    editor_insert_char(&app->editor, c);
                    editor_keep_cursor_visible(&app->editor, editor_rows(app), editor_cols(app));
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
        request_open(app, app->selected);
    } else if (row->expanded) {
        tree_set_expanded(&app->tree, app->selected, 0);
    } else if (access(row->path, R_OK | X_OK) != 0) {
        set_message(app, "Cannot open %s: %s", row->path, strerror(errno));
    } else {
        tree_set_expanded(&app->tree, app->selected, 1);
    }
}

static void on_click(App *app, int x, int y) {
    /* A question is waiting: only y, n or Esc may answer it. */
    if (app->mode != MODE_EDIT) return;

    for (int i = 0; i < BUTTON_COUNT; i++) {
        SDL_Rect rect = app_button_rect(app, (Button)i);
        if (point_in(&rect, x, y)) {
            press_button(app, (Button)i);
            return;
        }
    }

    SDL_Rect tree = app_tree_rect(app);
    if (point_in(&tree, x, y)) {
        on_tree_click(app, y);
        return;
    }

    SDL_Rect text = app_editor_rect(app);
    if (app->has_file && point_in(&text, x, y)) {
        Editor *e = &app->editor;
        editor_click(e, e->top + (y - text.y) / app->font.cell_h,
                     e->left + (x - text.x) / app->font.cell_w);
    }
}

static void on_wheel(App *app, const SDL_MouseWheelEvent *wheel) {
    int amount = wheel->integer_y * WHEEL_LINES;
    SDL_Rect tree = app_tree_rect(app);

    if (point_in(&tree, (int)wheel->mouse_x, (int)wheel->mouse_y)) {
        int top = app->tree_top - amount;
        app->tree_top = SDL_clamp(top, 0, SDL_max(app->tree.count - 1, 0));
    } else if (app->has_file) {
        Editor *e = &app->editor;
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
            }
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

static void draw_toolbar(const App *app) {
    const Theme *theme = theme_current();
    int w, h;
    output_size(app, &w, &h);
    fill_rect(app, 0, 0, w, bar_height(app), theme->bar);

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

static void draw_editor(const App *app) {
    const Theme *theme = theme_current();
    SDL_Rect rect = app_editor_rect(app);
    SDL_SetRenderClipRect(app->renderer, &rect);

    if (!app->has_file) {
        font_draw_text(&app->font, rect.x, rect.y,
                       "Select a file in the tree, or click New File.", theme->text);
    } else {
        const Editor *e = &app->editor;
        int rows = editor_rows(app);
        int cols = editor_cols(app);
        for (int i = 0; i < rows && e->top + i < e->buffer.count; i++) {
            draw_line(app, &e->buffer.lines[e->top + i], rect.y + i * app->font.cell_h, e->left, cols);
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

static void draw_status(const App *app) {
    const Theme *theme = theme_current();
    int w, h;
    output_size(app, &w, &h);
    int bar = bar_height(app);
    fill_rect(app, 0, h - bar, w, bar, theme->bar);

    char text[1024];
    int show_cursor = 0;
    if (app->mode == MODE_PROMPT) {
        snprintf(text, sizeof(text), "%s: %s", app->question, app->input);
        show_cursor = 1;
    } else if (app->mode == MODE_CONFIRM) {
        snprintf(text, sizeof(text), "%s", app->question);
    } else if (app->message[0] != '\0') {
        snprintf(text, sizeof(text), "%s", app->message);
    } else if (app->has_file) {
        const Editor *e = &app->editor;
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

void app_draw(App *app) {
    Color background = theme_current()->background;
    SDL_SetRenderDrawColor(app->renderer, background.r, background.g, background.b, 255);
    SDL_RenderClear(app->renderer);

    draw_editor(app);
    draw_tree(app);
    draw_toolbar(app);
    draw_status(app);
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
    close_file(app);
    tree_free(&app->tree);
    font_free(&app->font);
    free(app->selected);
    free(app->pending_open);
    app->selected = NULL;
    app->pending_open = NULL;
}
