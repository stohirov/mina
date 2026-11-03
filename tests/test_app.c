/* Tests for the application. They send it the same events a window would, and
 * draw to an in-memory surface after each one. */
#include <SDL3/SDL.h>
#include <sys/stat.h>

#include "app.h"
#include "check.h"
#include "theme.h"

#define EDITOR (app.tabs[app.active])
#define HAS_FILE (app.tab_count > 0)

static App app;
static SDL_Surface *surface;

static void send(SDL_Event *event) {
    app_handle_event(&app, event);
    app_draw(&app);
    SDL_RenderPresent(app.renderer);
}

static void key_mod(SDL_Keycode key, SDL_Keymod mod) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.mod = mod;
    send(&event);
}

static void key(SDL_Keycode code) {
    key_mod(code, SDL_KMOD_NONE);
}

static void text(const char *characters) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = characters;
    send(&event);
}

/* A real letter key sends KEY_DOWN and then TEXT_INPUT. */
static void press_letter(SDL_Keycode code, const char *character) {
    key(code);
    text(character);
}

static void click(int x, int y) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = (float)x;
    event.button.y = (float)y;
    send(&event);
}

static void click_button(Button button) {
    SDL_Rect r = app_button_rect(&app, button);
    click(r.x + 1, r.y + 1);
}

static void click_row(const char *path) {
    int index = tree_find(&app.tree, path);
    CHECK(index >= 0);
    SDL_Rect r = app_tree_rect(&app);
    click(r.x + 1, r.y + (index - app.tree_top) * app.font.cell_h + 1);
}

/* Click below the last row, to clear the selection. */
static void click_tree_empty(void) {
    SDL_Rect r = app_tree_rect(&app);
    click(r.x + 1, r.y + r.h - 1);
}

static void wheel(int x, int y, int ticks) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.integer_y = ticks;
    event.wheel.mouse_x = (float)x;
    event.wheel.mouse_y = (float)y;
    send(&event);
}

static void send_type(SDL_EventType type) {
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    send(&event);
}

static void right_click(int x, int y) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_RIGHT;
    event.button.x = (float)x;
    event.button.y = (float)y;
    send(&event);
}

static void right_click_row(const char *path) {
    int index = tree_find(&app.tree, path);
    CHECK(index >= 0);
    SDL_Rect r = app_tree_rect(&app);
    right_click(r.x + 1, r.y + (index - app.tree_top) * app.font.cell_h + 1);
}

static void right_click_tree_empty(void) {
    SDL_Rect r = app_tree_rect(&app);
    right_click(r.x + 1, r.y + r.h - 1);
}

/* Right-click the selected row, or empty space if none, and pick a menu item.
 * Does nothing if the menu did not open. */
static void menu_choose(MenuItem item) {
    if (app.selected != NULL && tree_find(&app.tree, app.selected) >= 0) {
        right_click_row(app.selected);
    } else {
        right_click_tree_empty();
    }
    if (app.mode != MODE_MENU) return;
    for (int i = 0; i < app.menu_count; i++) {
        if (app.menu_items[i] == item) {
            SDL_Rect r = app_menu_item_rect(&app, i);
            click(r.x + 1, r.y + 1);
            return;
        }
    }
    key(SDLK_ESCAPE);
}

static void new_item(MenuItem item, const char *name) {
    menu_choose(item);
    text(name);
    key(SDLK_RETURN);
}

static int tab_index(const char *path) {
    for (int i = 0; i < app.tab_count; i++) {
        if (strcmp(app.tabs[i].path, path) == 0) return i;
    }
    return -1;
}

static void click_tab(const char *path) {
    SDL_Rect r = app_tab_rect(&app, tab_index(path));
    click(r.x + 1, r.y + 1);
}

static void click_tab_close(const char *path) {
    SDL_Rect r = app_tab_close_rect(&app, tab_index(path));
    click(r.x + 1, r.y + 1);
}


static int pixel_is(int x, int y, Color c) {
    Uint8 r, g, b;
    SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, NULL);
    return r == c.r && g == c.g && b == c.b;
}

/* Is anything other than the background color drawn inside the rectangle? */
static int area_has_ink(int x, int y, int w, int h, Color background) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            if (!pixel_is(px, py, background)) return 1;
        }
    }
    return 0;
}

/* Start each test in a new empty folder with no file open. */
static void start_test(const char *name) {
    CHECK(chdir(name) != 0);
    CHECK(fs_create_folder(name) == 0);
    CHECK(chdir(name) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    CHECK(app.mode == MODE_EDIT);
}

static void end_test(void) {
    app_free(&app);
    CHECK(app_init(&app, app.renderer, 14) == 0);
    CHECK(chdir("..") == 0);
}

static void test_create_open_edit_save(void) {
    start_test("basic");

    /* Typing with nothing open is refused. */
    text("x");
    CHECK(!HAS_FILE);
    CHECK(strstr(app.message, "Open a file") != NULL);

    /* A new file opens straight away. */
    new_item(MENU_NEW_FILE, "note.txt");
    CHECK(file_is("note.txt", ""));
    CHECK(HAS_FILE);
    CHECK(strcmp(EDITOR.path, "note.txt") == 0);
    CHECK(strcmp(app.selected, "note.txt") == 0);

    text("line one");
    key(SDLK_RETURN);
    text("line two");
    CHECK(EDITOR.buffer.dirty == 1);
    CHECK(file_is("note.txt", ""));

    key_mod(SDLK_S, SDL_KMOD_GUI);
    CHECK(file_is("note.txt", "line one\nline two"));
    CHECK(EDITOR.buffer.dirty == 0);
    CHECK(strstr(app.message, "Saved") != NULL);

    key(SDLK_HOME);
    key(SDLK_DELETE);
    text("L");
    click_button(BUTTON_SAVE);
    CHECK(file_is("note.txt", "line one\nLine two"));

    key_mod(SDLK_S, SDL_KMOD_CTRL); /* Ctrl works as well as Cmd. */
    CHECK(strstr(app.message, "Saved") != NULL);

    key(SDLK_UP);
    key(SDLK_END);
    CHECK(EDITOR.row == 0 && EDITOR.col == 8);
    key_mod(SDLK_LEFT, SDL_KMOD_GUI);
    CHECK(EDITOR.col == 0);
    key_mod(SDLK_RIGHT, SDL_KMOD_GUI);
    CHECK(EDITOR.col == 8);

    key(SDLK_TAB);
    click_button(BUTTON_SAVE);
    CHECK(file_is("note.txt", "line one\t\nLine two"));

    key(SDLK_BACKSPACE);
    key(SDLK_RIGHT); /* Wraps to the next line. */
    key(SDLK_BACKSPACE); /* Joins the lines. */
    click_button(BUTTON_SAVE);
    CHECK(file_is("note.txt", "line oneLine two"));

    end_test();
}

static void test_prompts(void) {
    start_test("prompts");

    /* Esc cancels without creating anything. */
    menu_choose(MENU_NEW_FILE);
    CHECK(app.mode == MODE_PROMPT);
    text("nope");
    key(SDLK_ESCAPE);
    CHECK(app.mode == MODE_EDIT);
    CHECK(access("nope", F_OK) != 0);

    /* Backspace works, and Enter with no name does nothing. */
    menu_choose(MENU_NEW_FILE);
    key(SDLK_RETURN);
    CHECK(app.mode == MODE_PROMPT);
    text("abX");
    key(SDLK_BACKSPACE);
    key(SDLK_BACKSPACE);
    text("c");
    key(SDLK_RETURN);
    CHECK(access("ac", F_OK) == 0);

    /* Names holding a path are refused. */
    menu_choose(MENU_NEW_FILE);
    text("../escape");
    key(SDLK_RETURN);
    CHECK(strstr(app.message, "Invalid name") != NULL);
    CHECK(app.mode == MODE_EDIT);
    CHECK(access("../escape", F_OK) != 0);

    /* Creating over an existing name fails and keeps the file. */
    CHECK(fs_write_file("ac", "keep", 4) == 0);
    new_item(MENU_NEW_FILE, "ac");
    CHECK(strstr(app.message, "File exists") != NULL);
    CHECK(file_is("ac", "keep"));

    /* While a prompt is open, buttons and the tree do nothing. */
    menu_choose(MENU_NEW_FOLDER);
    menu_choose(MENU_DELETE);
    CHECK(app.action == ACTION_NEW_FOLDER);
    key(SDLK_ESCAPE);

    /* A long name cannot overflow the input. */
    menu_choose(MENU_NEW_FILE);
    for (int i = 0; i < 400; i++) text("a");
    CHECK(strlen(app.input) == sizeof(app.input) - 1);
    key(SDLK_ESCAPE);

    end_test();
}

static void test_tree_and_folders(void) {
    start_test("folders");

    /* Nothing selected: a new folder goes at the top. */
    new_item(MENU_NEW_FOLDER, "src");
    CHECK(access("src", F_OK) == 0);
    CHECK(strcmp(app.selected, "src") == 0);
    CHECK(!HAS_FILE);

    /* The selected folder receives new items and opens to show them. */
    new_item(MENU_NEW_FILE, "main.c");
    CHECK(access("src/main.c", F_OK) == 0);
    CHECK(tree_find(&app.tree, "src/main.c") >= 0);
    CHECK(strcmp(EDITOR.path, "src/main.c") == 0);

    /* The selected file's folder receives new items too. */
    new_item(MENU_NEW_FOLDER, "sub");
    CHECK(access("src/sub", F_OK) == 0);

    click_tree_empty();
    CHECK(app.selected == NULL);
    new_item(MENU_NEW_FILE, "top.txt");
    CHECK(access("top.txt", F_OK) == 0);

    /* Clicking a folder closes it, and clicking again opens it. */
    CHECK(tree_find(&app.tree, "src/main.c") >= 0);
    click_row("src");
    CHECK(tree_find(&app.tree, "src/main.c") == -1);
    click_row("src");
    CHECK(tree_find(&app.tree, "src/main.c") >= 0);

    /* Clicking a file opens it. */
    click_row("src/main.c");
    CHECK(strcmp(EDITOR.path, "src/main.c") == 0);

    /* Changes made outside the app appear when the window regains focus. */
    CHECK(fs_create_file("outside.txt") == 0);
    CHECK(tree_find(&app.tree, "outside.txt") == -1);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    CHECK(tree_find(&app.tree, "outside.txt") >= 0);

    /* A folder that cannot be read reports an error. */
    CHECK(fs_create_folder("locked") == 0);
    CHECK(chmod("locked", 0) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("locked");
    CHECK(strstr(app.message, "Cannot open locked") != NULL);
    CHECK(chmod("locked", 0755) == 0);

    /* Other things that cannot be opened report an error and change nothing. */
    CHECK(mkfifo("pipe", 0644) == 0);
    CHECK(symlink("nowhere", "broken") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("pipe");
    CHECK(strstr(app.message, "Cannot open pipe") != NULL);
    click_row("broken");
    CHECK(strstr(app.message, "Cannot open broken") != NULL);
    CHECK(strcmp(EDITOR.path, "src/main.c") == 0);

    end_test();
}

static void test_rename(void) {
    start_test("rename");
    menu_choose(MENU_RENAME);
    CHECK(app.mode == MODE_EDIT);

    new_item(MENU_NEW_FILE, "a.txt");
    click_tree_empty();
    new_item(MENU_NEW_FILE, "b.txt");
    CHECK(fs_write_file("a.txt", "A", 1) == 0);

    /* The prompt starts with the current name. */
    menu_choose(MENU_RENAME);
    CHECK(strcmp(app.input, "b.txt") == 0);

    /* Refuses to overwrite another file. */
    for (int i = 0; i < 5; i++) key(SDLK_BACKSPACE);
    text("a.txt");
    key(SDLK_RETURN);
    CHECK(strstr(app.message, "File exists") != NULL);
    CHECK(file_is("a.txt", "A"));

    /* Renaming the open file makes later saves go to the new name. */
    text("hello");
    menu_choose(MENU_RENAME);
    for (int i = 0; i < 5; i++) key(SDLK_BACKSPACE);
    text("c.txt");
    key(SDLK_RETURN);
    CHECK(access("b.txt", F_OK) != 0);
    CHECK(strcmp(EDITOR.path, "c.txt") == 0);
    CHECK(strcmp(app.selected, "c.txt") == 0);
    key_mod(SDLK_S, SDL_KMOD_GUI);
    CHECK(file_is("c.txt", "hello"));
    CHECK(access("b.txt", F_OK) != 0);

    /* Renaming a folder updates the path of an open file inside it. */
    click_tree_empty();
    new_item(MENU_NEW_FOLDER, "dir");
    new_item(MENU_NEW_FILE, "in.txt");
    click_row("dir");
    menu_choose(MENU_RENAME);
    for (int i = 0; i < 3; i++) key(SDLK_BACKSPACE);
    text("box");
    key(SDLK_RETURN);
    CHECK(access("box/in.txt", F_OK) == 0);
    CHECK(strcmp(EDITOR.path, "box/in.txt") == 0);
    text("z");
    key_mod(SDLK_S, SDL_KMOD_GUI);
    CHECK(file_is("box/in.txt", "z"));

    /* A folder whose name is only the start of "box" is a different folder. */
    click_tree_empty();
    new_item(MENU_NEW_FOLDER, "bo");
    menu_choose(MENU_RENAME);
    for (int i = 0; i < 2; i++) key(SDLK_BACKSPACE);
    text("other");
    key(SDLK_RETURN);
    CHECK(access("other", F_OK) == 0);
    CHECK(strcmp(EDITOR.path, "box/in.txt") == 0);

    end_test();
}

static void test_delete(void) {
    start_test("delete");
    menu_choose(MENU_DELETE);
    CHECK(app.mode == MODE_EDIT);

    new_item(MENU_NEW_FOLDER, "box");
    new_item(MENU_NEW_FILE, "in.txt");
    click_tree_empty();
    new_item(MENU_NEW_FILE, "keep.txt");
    new_item(MENU_NEW_FILE, "gone.txt");

    /* n and Esc keep the file. */
    menu_choose(MENU_DELETE);
    CHECK(app.mode == MODE_CONFIRM);
    press_letter(SDLK_N, "n");
    CHECK(access("gone.txt", F_OK) == 0);
    CHECK(app.mode == MODE_EDIT);
    CHECK(EDITOR.buffer.dirty == 0); /* The n was not typed into the file. */

    menu_choose(MENU_DELETE);
    key(SDLK_ESCAPE);
    CHECK(access("gone.txt", F_OK) == 0);

    /* Other keys do not answer the question. */
    menu_choose(MENU_DELETE);
    press_letter(SDLK_X, "x");
    key(SDLK_RETURN);
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(access("gone.txt", F_OK) == 0);

    /* y deletes the open file and closes it. */
    press_letter(SDLK_Y, "y");
    CHECK(access("gone.txt", F_OK) != 0);
    CHECK(tab_index("gone.txt") == -1);
    CHECK(app.selected == NULL);
    CHECK(tree_find(&app.tree, "gone.txt") == -1);

    /* Deleting a folder deletes what is inside and closes a file open there. */
    click_row("box/in.txt");
    CHECK(tab_index("box/in.txt") >= 0);
    click_row("box");
    menu_choose(MENU_DELETE);
    CHECK(strstr(app.question, "everything in it") != NULL);
    press_letter(SDLK_Y, "y");
    CHECK(access("box", F_OK) != 0);
    CHECK(tab_index("box/in.txt") == -1);

    /* Deleting something else leaves the open file open. */
    click_row("keep.txt");
    CHECK(fs_create_file("other.txt") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    text("unsaved");
    click_tree_empty();
    CHECK(fs_create_folder("d") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("d");
    menu_choose(MENU_DELETE);
    press_letter(SDLK_Y, "y");
    CHECK(HAS_FILE);
    CHECK(strcmp(EDITOR.path, "keep.txt") == 0);
    CHECK(EDITOR.buffer.dirty == 1);
    CHECK(EDITOR.buffer.lines[0].len == 7);

    /* A link is removed without touching its target. */
    CHECK(fs_create_folder("precious") == 0);
    CHECK(fs_create_file("precious/file") == 0);
    CHECK(symlink("precious", "link") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("link");
    menu_choose(MENU_DELETE);
    press_letter(SDLK_Y, "y");
    CHECK(access("precious/file", F_OK) == 0);

    end_test();
}

static void test_tabs(void) {
    start_test("tabs");
    CHECK(fs_write_file("a.txt", "aaa", 3) == 0);
    CHECK(fs_write_file("b.txt", "bbb", 3) == 0);
    CHECK(fs_write_file("c.txt", "ccc", 3) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);

    /* Each file opens once, in its own tab. */
    click_row("a.txt");
    click_row("b.txt");
    CHECK(app.tab_count == 2);
    CHECK(strcmp(EDITOR.path, "b.txt") == 0);
    click_row("a.txt");
    CHECK(app.tab_count == 2);
    CHECK(strcmp(EDITOR.path, "a.txt") == 0);

    /* Switching never asks, and each tab keeps its text, cursor and history. */
    text("X");
    CHECK(EDITOR.buffer.dirty == 1);
    click_tab("b.txt");
    CHECK(app.mode == MODE_EDIT);
    CHECK(strcmp(EDITOR.path, "b.txt") == 0);
    CHECK(strcmp(app.selected, "b.txt") == 0);
    CHECK(EDITOR.buffer.dirty == 0);
    CHECK(EDITOR.buffer.lines[0].text[0] == 'b');
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(strstr(app.message, "Nothing to undo") != NULL);
    click_tab("a.txt");
    CHECK(strcmp(app.selected, "a.txt") == 0);
    CHECK(strcmp(EDITOR.buffer.lines[0].text, "Xaaa") == 0);
    CHECK(EDITOR.col == 1);
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(strcmp(EDITOR.buffer.lines[0].text, "aaa") == 0);
    CHECK(EDITOR.buffer.dirty == 0);

    /* Saving only touches the visible tab. */
    text("Y");
    click_tab("b.txt");
    text("Z");
    key_mod(SDLK_S, SDL_KMOD_GUI);
    CHECK(file_is("b.txt", "Zbbb"));
    CHECK(file_is("a.txt", "aaa"));
    CHECK(app.tabs[tab_index("a.txt")].buffer.dirty == 1);
    click_tab("a.txt");
    key_mod(SDLK_Z, SDL_KMOD_GUI);

    /* Ctrl-Tab goes round the tabs, and does not type a tab character. */
    click_row("c.txt");
    CHECK(app.active == 2);
    key_mod(SDLK_TAB, SDL_KMOD_CTRL);
    CHECK(app.active == 0);
    key_mod(SDLK_TAB, SDL_KMOD_CTRL);
    CHECK(app.active == 1);
    key_mod(SDLK_TAB, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    CHECK(app.active == 0);
    key_mod(SDLK_TAB, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    CHECK(app.active == 2);
    CHECK(EDITOR.buffer.dirty == 0);

    /* Closing an unmodified tab asks nothing, and its neighbour takes over. */
    click_tab_close("b.txt"); /* An inactive tab: the visible one stays. */
    CHECK(app.tab_count == 2);
    CHECK(tab_index("b.txt") == -1);
    CHECK(strcmp(EDITOR.path, "c.txt") == 0);
    click_tab("a.txt");
    CHECK(strcmp(EDITOR.path, "a.txt") == 0);
    key_mod(SDLK_W, SDL_KMOD_GUI);
    CHECK(app.tab_count == 1);
    CHECK(strcmp(EDITOR.path, "c.txt") == 0);
    CHECK(strcmp(app.selected, "c.txt") == 0);
    key_mod(SDLK_W, SDL_KMOD_CTRL);
    CHECK(app.tab_count == 0);
    key_mod(SDLK_W, SDL_KMOD_GUI); /* Nothing left: does nothing. */
    key_mod(SDLK_TAB, SDL_KMOD_CTRL);
    CHECK(app.tab_count == 0);

    /* Reopening after closing starts fresh. */
    click_row("a.txt");
    CHECK(app.tab_count == 1);
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(strstr(app.message, "Nothing to undo") != NULL);
    end_test();
}

static void test_closing_unsaved_tabs(void) {
    start_test("closing");
    CHECK(fs_write_file("a.txt", "", 0) == 0);
    CHECK(fs_write_file("b.txt", "", 0) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("a.txt");
    click_row("b.txt");

    /* Closing an inactive modified tab shows it and asks. Esc keeps it. */
    click_tab("a.txt");
    text("A");
    click_tab("b.txt");
    click_tab_close("a.txt");
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(strstr(app.question, "Save changes to a.txt") != NULL);
    CHECK(strcmp(EDITOR.path, "a.txt") == 0);
    key(SDLK_ESCAPE);
    CHECK(app.tab_count == 2);
    CHECK(EDITOR.buffer.dirty == 1);

    /* While asking, clicks and other keys do nothing. */
    key_mod(SDLK_W, SDL_KMOD_GUI);
    click_tab("b.txt");
    click_row("b.txt");
    press_letter(SDLK_X, "x");
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(strcmp(EDITOR.path, "a.txt") == 0);

    /* n discards. The answer must not be typed into the next tab. */
    press_letter(SDLK_N, "n");
    CHECK(app.tab_count == 1);
    CHECK(strcmp(EDITOR.path, "b.txt") == 0);
    CHECK(EDITOR.buffer.dirty == 0);
    CHECK(EDITOR.buffer.lines[0].len == 0);
    CHECK(file_is("a.txt", ""));

    /* y saves and closes, again without typing the answer. */
    click_row("a.txt");
    text("A");
    key_mod(SDLK_W, SDL_KMOD_GUI);
    press_letter(SDLK_Y, "y");
    CHECK(file_is("a.txt", "A"));
    CHECK(app.tab_count == 1);
    CHECK(strcmp(EDITOR.path, "b.txt") == 0);
    CHECK(EDITOR.buffer.lines[0].len == 0);

    /* A failed save keeps the tab open with its changes. */
    CHECK(chmod("b.txt", 0444) == 0);
    text("B");
    key_mod(SDLK_W, SDL_KMOD_GUI);
    press_letter(SDLK_Y, "y");
    CHECK(strstr(app.message, "Save failed") != NULL);
    CHECK(app.tab_count == 1);
    CHECK(EDITOR.buffer.dirty == 1);
    CHECK(app.mode == MODE_EDIT);
    CHECK(file_is("b.txt", ""));
    CHECK(chmod("b.txt", 0644) == 0);

    /* Quitting asks about each modified tab in turn. */
    click_row("a.txt");
    text("2");
    send_type(SDL_EVENT_QUIT);
    CHECK(app.running == 1);
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(strstr(app.question, "b.txt") != NULL); /* Tabs are asked about in order. */

    /* Esc cancels the whole quit. */
    key(SDLK_ESCAPE);
    CHECK(app.running == 1);
    CHECK(app.pending_quit == 0);
    CHECK(app.tab_count == 2);

    key_mod(SDLK_Q, SDL_KMOD_GUI);
    press_letter(SDLK_Y, "y"); /* Saves b.txt. */
    CHECK(app.running == 1);
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(app.pending_quit == 1);
    CHECK(strstr(app.question, "a.txt") != NULL);
    press_letter(SDLK_N, "n"); /* Discards the change to a.txt. */
    CHECK(app.running == 0);
    CHECK(file_is("b.txt", "B"));
    CHECK(file_is("a.txt", "A"));
    end_test();

    /* A failed save stops the quit. */
    CHECK(chdir("closing") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    CHECK(chmod("a.txt", 0444) == 0);
    click_row("a.txt");
    text("!");
    send_type(SDL_EVENT_QUIT);
    press_letter(SDLK_Y, "y");
    CHECK(app.running == 1);
    CHECK(app.pending_quit == 0);
    CHECK(app.tab_count == 1);
    CHECK(chmod("a.txt", 0644) == 0);
    app_free(&app);
    CHECK(app_init(&app, app.renderer, 14) == 0);
    CHECK(chdir("..") == 0);
}

static void test_tabs_follow_files(void) {
    start_test("follow");
    new_item(MENU_NEW_FOLDER, "dir");
    new_item(MENU_NEW_FILE, "one.txt");
    click_row("dir");
    new_item(MENU_NEW_FILE, "two.txt");
    click_tree_empty();
    new_item(MENU_NEW_FILE, "top.txt");
    CHECK(app.tab_count == 3);

    /* Renaming a folder updates every tab inside it, even when not visible. */
    click_row("dir");
    menu_choose(MENU_RENAME);
    for (int i = 0; i < 3; i++) key(SDLK_BACKSPACE);
    text("box");
    key(SDLK_RETURN);
    CHECK(tab_index("box/one.txt") >= 0);
    CHECK(tab_index("box/two.txt") >= 0);
    CHECK(strcmp(EDITOR.path, "top.txt") == 0);

    /* Their names are shown, and saving goes to the new place. */
    click_tab("box/one.txt");
    text("1");
    key_mod(SDLK_S, SDL_KMOD_GUI);
    CHECK(file_is("box/one.txt", "1"));

    /* Deleting the folder closes its tabs and leaves the others. */
    click_tab("top.txt");
    CHECK(strcmp(app.selected, "top.txt") == 0);
    CHECK(tree_find(&app.tree, "box") >= 0);
    click_row("box"); /* The rename kept it closed: this opens it. */
    menu_choose(MENU_DELETE);
    press_letter(SDLK_Y, "y");
    CHECK(app.tab_count == 1);
    CHECK(strcmp(EDITOR.path, "top.txt") == 0);
    end_test();

    /* Removing a tab before the visible one keeps the same file visible. */
    start_test("follow2");
    new_item(MENU_NEW_FILE, "a");
    click_tree_empty();
    new_item(MENU_NEW_FILE, "b");
    click_tree_empty();
    new_item(MENU_NEW_FILE, "c");
    CHECK(strcmp(EDITOR.path, "c") == 0);
    click_tab_close("a");
    CHECK(strcmp(EDITOR.path, "c") == 0);
    click_row("b");
    menu_choose(MENU_DELETE);
    press_letter(SDLK_Y, "y");
    CHECK(app.tab_count == 1);
    CHECK(strcmp(EDITOR.path, "c") == 0);
    end_test();
}

static void test_many_tabs(void) {
    start_test("manytabs");
    for (int i = 0; i < 40; i++) {
        char name[32];
        snprintf(name, sizeof(name), "file_number_%02d.txt", i);
        CHECK(fs_create_file(name) == 0);
    }
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    for (int i = 0; i < 25; i++) {
        char name[32];
        snprintf(name, sizeof(name), "file_number_%02d.txt", i);
        click_row(name);
    }
    CHECK(app.tab_count == 25);

    /* The visible tab is always fully on screen, left of the buttons. */
    SDL_Rect save = app_button_rect(&app, BUTTON_OPEN_FOLDER);
    SDL_Rect active = app_tab_rect(&app, app.active);
    CHECK(app.first_tab > 0);
    CHECK(active.w > 0);
    CHECK(active.x + active.w <= save.x);

    /* Going back to the first scrolls to it. */
    key_mod(SDLK_TAB, SDL_KMOD_CTRL);
    CHECK(app.active == 0);
    CHECK(app.first_tab == 0);
    CHECK(app_tab_rect(&app, 0).w > 0);

    /* The wheel scrolls the tabs, without changing the visible one. */
    SDL_Rect first = app_tab_rect(&app, 0);
    wheel(first.x + 1, first.y + 1, -3);
    CHECK(app.first_tab == 3);
    CHECK(app.active == 0);
    CHECK(app_tab_rect(&app, 0).w == 0);
    wheel(first.x + 1, first.y + 1, 1000);
    CHECK(app.first_tab == 0);
    wheel(first.x + 1, first.y + 1, -1000);
    CHECK(app.first_tab == 24);

    /* A click at the end of the tabs does not land on a hidden one. */
    click(app_button_rect(&app, BUTTON_OPEN_FOLDER).x - 1, first.y + 1);
    CHECK(app.tab_count == 25);

    /* Closing them all from the last leaves nothing scrolled away. */
    for (int i = 0; i < 25; i++) key_mod(SDLK_W, SDL_KMOD_GUI);
    CHECK(app.tab_count == 0);
    CHECK(app.first_tab == 0);
    end_test();

    /* A very long name is cut short, with its close button still in reach. */
    start_test("longname");
    const char *name = "a_very_long_file_name_that_goes_on_and_on_and_on.txt";
    CHECK(fs_create_file(name) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row(name);
    SDL_Rect tab = app_tab_rect(&app, 0);
    CHECK(tab.w < app.font.cell_w * 32);
    click_tab_close(name);
    CHECK(app.tab_count == 0);
    end_test();
}

static void test_right_click_menu(void) {
    start_test("menu");
    CHECK(fs_create_folder("dir") == 0);
    CHECK(fs_write_file("dir/in.txt", "in", 2) == 0);
    CHECK(fs_write_file("f.txt", "f", 1) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);

    /* A file offers all four items, selects the row, and does not open it. */
    right_click_row("f.txt");
    CHECK(app.mode == MODE_MENU);
    CHECK(app.menu_count == 4);
    CHECK(app.menu_items[2] == MENU_RENAME);
    CHECK(app.menu_items[3] == MENU_DELETE);
    CHECK(strcmp(app.selected, "f.txt") == 0);
    CHECK(!HAS_FILE);

    /* Esc closes it, and typing while it is open goes nowhere. */
    text("q");
    key(SDLK_ESCAPE);
    CHECK(app.mode == MODE_EDIT);

    /* A folder offers the same, and stays closed. */
    right_click_row("dir");
    CHECK(app.menu_count == 4);
    CHECK(tree_find(&app.tree, "dir/in.txt") == -1);
    key(SDLK_ESCAPE);

    /* Empty space offers only what makes sense there, and clears the selection. */
    right_click_tree_empty();
    CHECK(app.menu_count == 2);
    CHECK(app.selected == NULL);
    CHECK(app.menu_items[0] == MENU_NEW_FILE);
    CHECK(app.menu_items[1] == MENU_NEW_FOLDER);
    key(SDLK_ESCAPE);

    /* Clicking outside closes it and does nothing else. */
    right_click_row("f.txt");
    SDL_Rect theme = app_button_rect(&app, BUTTON_THEME);
    const char *before = theme_current()->name;
    click(theme.x + 1, theme.y + 1);
    CHECK(app.mode == MODE_EDIT);
    CHECK(strcmp(theme_current()->name, before) == 0);
    CHECK(access("f.txt", F_OK) == 0);

    /* Right-clicking elsewhere moves it to the new row. */
    right_click_row("f.txt");
    right_click_row("dir");
    CHECK(app.mode == MODE_MENU);
    CHECK(strcmp(app.selected, "dir") == 0);
    key(SDLK_ESCAPE);

    /* Right-clicking outside the tree does nothing. */
    SDL_Rect e = app_editor_rect(&app);
    right_click(e.x + 1, e.y + 1);
    CHECK(app.mode == MODE_EDIT);

    /* It stays inside the window near the corner. */
    SDL_Rect t = app_tree_rect(&app);
    right_click(t.x + 1, t.y + t.h - 1);
    SDL_Rect last = app_menu_item_rect(&app, app.menu_count - 1);
    CHECK(last.y + last.h <= 700);
    key(SDLK_ESCAPE);

    /* The item under the mouse is tracked, and highlighted. */
    right_click_row("f.txt");
    SDL_Rect item = app_menu_item_rect(&app, 1);
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = (float)(item.x + 1);
    event.motion.y = (float)(item.y + 1);
    send(&event);
    CHECK(app.menu_hover == 1);
    CHECK(pixel_is(item.x + item.w - 2, item.y + 1, theme_current()->selected));
    event.motion.x = (float)(item.x + item.w + 5);
    send(&event);
    CHECK(app.menu_hover == -1);
    key(SDLK_ESCAPE);

    /* New items go in the folder that was right-clicked. */
    click_tree_empty();
    right_click_row("dir");
    click(app_menu_item_rect(&app, 0).x + 1, app_menu_item_rect(&app, 0).y + 1);
    CHECK(app.mode == MODE_PROMPT);
    text("made.txt");
    key(SDLK_RETURN);
    CHECK(access("dir/made.txt", F_OK) == 0);
    CHECK(strcmp(EDITOR.path, "dir/made.txt") == 0);
    CHECK(tree_find(&app.tree, "dir/made.txt") >= 0);

    /* The menu is not available while a question is waiting. */
    menu_choose(MENU_RENAME);
    CHECK(app.mode == MODE_PROMPT);
    right_click_row("f.txt");
    CHECK(app.mode == MODE_PROMPT);
    CHECK(strcmp(app.selected, "dir/made.txt") == 0);
    key(SDLK_ESCAPE);
    end_test();
}

static void test_layout(void) {
    start_test("layout");
    SDL_Rect theme = app_button_rect(&app, BUTTON_THEME);
    SDL_Rect save = app_button_rect(&app, BUTTON_SAVE);
    SDL_Rect t = app_tree_rect(&app);
    SDL_Rect e = app_editor_rect(&app);

    /* Theme is in the top right corner, with Save just to its left. */
    CHECK(theme.x + theme.w <= 1000);
    CHECK(theme.x + theme.w >= 1000 - app.font.cell_w);
    CHECK(save.x + save.w <= theme.x);
    CHECK(theme.x - (save.x + save.w) <= app.font.cell_w);

    /* Open Folder is to the left of Save. */
    SDL_Rect open_folder = app_button_rect(&app, BUTTON_OPEN_FOLDER);
    CHECK(open_folder.x + open_folder.w <= save.x);
    CHECK(open_folder.x > app_tree_rect(&app).x + app_tree_rect(&app).w);
    CHECK(theme.y < e.y && save.y < e.y);

    /* The tree runs from the top of the window down to the status bar. */
    CHECK(t.y == 0);
    CHECK(t.y + t.h == e.y + e.h);
    CHECK(pixel_is(t.x + t.w - 2, t.y + 1, theme_current()->sidebar));

    /* With nothing open, the Save button says so. */
    click(save.x + 1, save.y + 1);
    CHECK(strstr(app.message, "No file is open") != NULL);
    end_test();
}

static void test_quit(void) {
    start_test("quit");
    send_type(SDL_EVENT_QUIT);
    CHECK(app.running == 0);
    end_test();

    /* Discarding on quit leaves the file alone. */
    CHECK(fs_write_file("quit/q.txt", "q", 1) == 0);
    CHECK(chdir("quit") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("q.txt");
    text("x");
    send_type(SDL_EVENT_QUIT);
    press_letter(SDLK_N, "n");
    CHECK(app.running == 0);
    CHECK(file_is("q.txt", "q"));
    app_free(&app);
    CHECK(app_init(&app, app.renderer, 14) == 0);
    CHECK(chdir("..") == 0);
}

static void test_non_english_input_and_exact_saves(void) {
    start_test("bytes");
    const char raw[] = "h\xc3\xa9llo\r\n\tx\x01y\n";
    CHECK(fs_write_file("raw.txt", raw, sizeof(raw) - 1) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("raw.txt");

    /* Non-English input is refused, leaving the file unchanged. */
    text("\xd0\xb6");
    CHECK(EDITOR.buffer.dirty == 0);
    CHECK(strstr(app.message, "English") != NULL);

    /* A later edit keeps every other byte exactly as it was. */
    text("Z");
    key(SDLK_BACKSPACE);
    click_button(BUTTON_SAVE);
    CHECK(file_is("raw.txt", raw));
    end_test();
}

static void test_mouse_in_editor(void) {
    start_test("mouse");
    CHECK(fs_write_file("m.txt", "first\n\tsecond\nthird", 19) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("m.txt");

    SDL_Rect r = app_editor_rect(&app);
    int w = app.font.cell_w, h = app.font.cell_h;
    click(r.x + w * 2 + 1, r.y + h * 0 + 1);
    CHECK(EDITOR.row == 0 && EDITOR.col == 2);
    click(r.x + w * 5 + 1, r.y + h * 1 + 1); /* After the tab. */
    CHECK(EDITOR.row == 1 && EDITOR.col == 2);
    click(r.x + w * 50, r.y + h * 2 + 1);
    CHECK(EDITOR.row == 2 && EDITOR.col == 5);
    click(r.x + 1, r.y + r.h - 1); /* Below the last line. */
    CHECK(EDITOR.row == 2 && EDITOR.col == 0);
    end_test();
}

static void test_scrolling(void) {
    start_test("scroll");
    SDL_Rect e = app_editor_rect(&app);
    int rows = e.h / app.font.cell_h;

    char big[2000] = "";
    for (int i = 0; i < 100; i++) strcat(big, "row\n");
    CHECK(fs_write_file("big.txt", big, strlen(big)) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("big.txt");
    int last = EDITOR.buffer.count - 1;
    CHECK(last == 100);

    key(SDLK_PAGEDOWN);
    CHECK(EDITOR.row == rows);
    CHECK(EDITOR.top == 1);
    for (int i = 0; i < 20; i++) key(SDLK_PAGEDOWN);
    CHECK(EDITOR.row == last);
    CHECK(EDITOR.top == last - rows + 1);

    /* The wheel scrolls without moving the cursor, and stops at both ends. */
    int top = EDITOR.top;
    wheel(e.x + 1, e.y + 1, 1);
    CHECK(EDITOR.top < top);
    CHECK(EDITOR.row == last);
    wheel(e.x + 1, e.y + 1, 1000);
    CHECK(EDITOR.top == 0);
    wheel(e.x + 1, e.y + 1, -1000);
    CHECK(EDITOR.top == last);
    wheel(e.x + 1, e.y + 1, 1000);

    /* Typing brings the cursor back into view. */
    text("x");
    CHECK(EDITOR.top == last - rows + 1);

    /* A long line scrolls sideways, and the text stays inside its area. */
    int cols = e.w / app.font.cell_w;
    for (int i = 0; i < cols + 5; i++) text("y");
    CHECK(EDITOR.left > 0);
    key(SDLK_HOME);
    CHECK(EDITOR.left == 0);

    /* The tree scrolls, and clicks land on the right row afterwards. */
    for (int i = 0; i < 100; i++) {
        char name[16];
        snprintf(name, sizeof(name), "f%03d", i);
        CHECK(fs_create_file(name) == 0);
    }
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    SDL_Rect t = app_tree_rect(&app);
    wheel(t.x + 1, t.y + 1, -4);
    CHECK(app.tree_top > 0);
    click_button(BUTTON_SAVE);
    click_row("f050");
    CHECK(strcmp(EDITOR.path, "f050") == 0);
    wheel(t.x + 1, t.y + 1, 1000);
    CHECK(app.tree_top == 0);
    wheel(t.x + 1, t.y + 1, -1000);
    CHECK(app.tree_top == app.tree.count - 1);
    click_tree_empty();
    CHECK(app.selected == NULL);
    end_test();
}

static void key_shift(SDL_Keycode code) {
    key_mod(code, SDL_KMOD_SHIFT);
}

static void mouse_motion(int x, int y) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.state = SDL_BUTTON_LMASK;
    event.motion.x = (float)x;
    event.motion.y = (float)y;
    send(&event);
}

static void mouse_up(void) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    send(&event);
}

/* Pixel position of a text row and screen column, with no scrolling. */
static int col_x(int col) {
    return app_editor_rect(&app).x + col * app.font.cell_w + 1;
}

static int row_y(int row) {
    return app_editor_rect(&app).y + row * app.font.cell_h + 1;
}

static int selected_is(const char *expected) {
    char *selected = editor_selected_text(&EDITOR);
    int same = expected == NULL ? selected == NULL
                                : (selected != NULL && strcmp(selected, expected) == 0);
    free(selected);
    return same;
}

static int clipboard_is(const char *expected) {
    char *current = SDL_GetClipboardText();
    int same = current != NULL && strcmp(current, expected) == 0;
    SDL_free(current);
    return same;
}

static void test_mouse_selection(void) {
    start_test("drag");
    CHECK(fs_write_file("d.txt", "first\nsecond\nthird", 18) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("d.txt");

    click(col_x(1), row_y(0));
    CHECK(selected_is(NULL));
    mouse_motion(col_x(3), row_y(0));
    CHECK(selected_is("ir"));
    mouse_motion(col_x(2), row_y(1));
    CHECK(selected_is("irst\nse"));
    mouse_up();

    /* Moving after release changes nothing. */
    mouse_motion(col_x(5), row_y(2));
    CHECK(selected_is("irst\nse"));

    /* A plain click clears the selection. */
    click(col_x(0), row_y(2));
    mouse_up();
    CHECK(selected_is(NULL));

    /* Shift-click extends from the cursor. */
    SDL_SetModState(SDL_KMOD_SHIFT);
    click(col_x(3), row_y(2));
    SDL_SetModState(SDL_KMOD_NONE);
    mouse_up();
    CHECK(selected_is("thi"));

    /* A drag that started outside the text selects nothing. */
    click(col_x(0), row_y(0));
    mouse_up();
    click_tree_empty();
    mouse_motion(col_x(4), row_y(1));
    CHECK(selected_is(NULL));
    mouse_up();

    /* A missed button release does not leave the drag stuck. */
    click(col_x(0), row_y(0));
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = (float)col_x(4);
    event.motion.y = (float)row_y(0);
    send(&event);
    CHECK(app.dragging == 0);
    CHECK(selected_is(NULL));
    end_test();

    /* Dragging past the edges of the text area scrolls. */
    start_test("dragscroll");
    char big[2000] = "";
    for (int i = 0; i < 100; i++) strcat(big, "row\n");
    CHECK(fs_write_file("b.txt", big, strlen(big)) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("b.txt");
    SDL_Rect e = app_editor_rect(&app);

    click(col_x(0), row_y(0));
    for (int i = 0; i < 10; i++) mouse_motion(col_x(0), e.y + e.h + 5);
    CHECK(EDITOR.top > 0);
    CHECK(EDITOR.selecting);
    int top = EDITOR.top;
    for (int i = 0; i < 3; i++) mouse_motion(col_x(0), e.y - 5);
    CHECK(EDITOR.top < top);
    CHECK(EDITOR.row == EDITOR.top);
    mouse_up();
    end_test();
}

static void test_copy_and_paste(void) {
    start_test("clipboard");
    CHECK(fs_write_file("c.txt", "hello world\nsecond", 18) == 0);
    CHECK(fs_write_file("other.txt", "", 0) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);

    /* With no file open these do nothing. */
    SDL_SetClipboardText("unchanged");
    key_mod(SDLK_C, SDL_KMOD_GUI);
    key_mod(SDLK_V, SDL_KMOD_GUI);
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(!HAS_FILE);
    CHECK(clipboard_is("unchanged"));

    click_row("c.txt");

    /* Copy with nothing selected keeps the clipboard and says why. */
    key_mod(SDLK_C, SDL_KMOD_GUI);
    CHECK(strstr(app.message, "Select some text") != NULL);
    CHECK(clipboard_is("unchanged"));

    /* Shift+arrows select, and copying does not change the file. */
    for (int i = 0; i < 5; i++) key_shift(SDLK_RIGHT);
    CHECK(selected_is("hello"));
    key_mod(SDLK_C, SDL_KMOD_GUI);
    CHECK(clipboard_is("hello"));
    CHECK(strstr(app.message, "Copied") != NULL);
    CHECK(EDITOR.buffer.dirty == 0);
    CHECK(selected_is("hello"));

    /* Ctrl works as well as Cmd. */
    key(SDLK_END);
    key_mod(SDLK_V, SDL_KMOD_CTRL);
    click_button(BUTTON_SAVE);
    CHECK(file_is("c.txt", "hello worldhello\nsecond"));
    CHECK(EDITOR.col == 16);

    /* Pasting over a selection replaces it, and can span lines. */
    SDL_SetClipboardText("one\ntwo");
    key(SDLK_HOME);
    key_shift(SDLK_DOWN);
    key_mod(SDLK_V, SDL_KMOD_GUI);
    click_button(BUTTON_SAVE);
    CHECK(file_is("c.txt", "one\ntwosecond"));
    CHECK(EDITOR.row == 1 && EDITOR.col == 3);

    /* Copy across lines. */
    key_mod(SDLK_RIGHT, SDL_KMOD_GUI);
    key_shift(SDLK_UP);
    key_mod(SDLK_LEFT, SDL_KMOD_GUI | SDL_KMOD_SHIFT);
    CHECK(selected_is("one\ntwosecond"));
    key_mod(SDLK_C, SDL_KMOD_GUI);
    CHECK(clipboard_is("one\ntwosecond"));

    /* A paste of many lines scrolls the cursor into view. */
    char lines[1000] = "";
    for (int i = 0; i < 100; i++) strcat(lines, "line\n");
    SDL_SetClipboardText(lines);
    key(SDLK_END);
    key_mod(SDLK_V, SDL_KMOD_GUI);
    CHECK(EDITOR.top > 0);
    int rows = app_editor_rect(&app).h / app.font.cell_h;
    CHECK(EDITOR.row < EDITOR.top + rows);

    /* One Cmd-Z removes the whole paste. */
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(EDITOR.buffer.count == 2);
    CHECK(EDITOR.buffer.dirty == 0);

    /* Non-English text pasted in keeps its bytes, like the rest of a file. */
    SDL_SetClipboardText("\xc3\xa9");
    key_mod(SDLK_V, SDL_KMOD_GUI);
    click_button(BUTTON_SAVE);
    CHECK(strstr(EDITOR.buffer.lines[0].text, "\xc3\xa9") != NULL);

    /* The clipboard outlives the file it was copied from. */
    SDL_SetClipboardText("across");
    click_row("other.txt");
    CHECK(strcmp(EDITOR.path, "other.txt") == 0);
    key_mod(SDLK_V, SDL_KMOD_GUI);
    click_button(BUTTON_SAVE);
    CHECK(file_is("other.txt", "across"));
    end_test();
}

static void test_undo(void) {
    start_test("undo");
    CHECK(fs_write_file("u.txt", "base", 4) == 0);
    CHECK(fs_write_file("other.txt", "other", 5) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("u.txt");

    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(strstr(app.message, "Nothing to undo") != NULL);

    text("a");
    text("b");
    key(SDLK_RETURN);
    text("c");
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    click_button(BUTTON_SAVE);
    CHECK(file_is("u.txt", "abbase"));
    key_mod(SDLK_Z, SDL_KMOD_CTRL);
    CHECK(EDITOR.buffer.dirty == 1);
    CHECK(EDITOR.col == 0);

    /* Back at the saved text, switching files asks nothing. */
    text("x");
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    key(SDLK_END);
    text("y");
    click_button(BUTTON_SAVE);
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    click_button(BUTTON_SAVE);
    CHECK(file_is("u.txt", "base"));
    text("z");
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(EDITOR.buffer.dirty == 0);
    click_row("other.txt");
    CHECK(app.mode == MODE_EDIT);
    CHECK(strcmp(EDITOR.path, "other.txt") == 0);

    /* Each file has its own history. */
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(strstr(app.message, "Nothing to undo") != NULL);

    /* Undo scrolls back to the change. */
    for (int i = 0; i < 100; i++) key(SDLK_RETURN);
    CHECK(EDITOR.top > 0);
    for (int i = 0; i < 100; i++) key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(EDITOR.top == 0);
    CHECK(EDITOR.buffer.dirty == 0);

    /* Cmd-Shift-Z is not undo. */
    text("k");
    key_mod(SDLK_Z, SDL_KMOD_GUI | SDL_KMOD_SHIFT);
    CHECK(EDITOR.buffer.dirty == 1);
    end_test();
}

static void test_selection_is_drawn(void) {
    start_test("highlight");
    CHECK(fs_write_file("s.txt", "abc\n\ndef", 8) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("s.txt");

    SDL_Rect e = app_editor_rect(&app);
    int w = app.font.cell_w, h = app.font.cell_h;
    Color selected = theme_current()->selected;
    Color background = theme_current()->background;

    /* Top-left corner of a text cell: the background shows there, not ink. */
#define CELL(col, row) pixel_is(e.x + (col) * w, e.y + (row) * h, selected)

    CHECK(!CELL(1, 0));

    click(col_x(1), row_y(0));
    mouse_motion(col_x(1), row_y(2));
    mouse_up();
    CHECK(!CELL(0, 0));  /* Before the start. */
    CHECK(CELL(2, 0));   /* Inside the first line. */
    CHECK(CELL(3, 0));   /* The first line's line break. */
    CHECK(!CELL(4, 0));
    CHECK(CELL(0, 1));   /* The empty line's line break. */
    CHECK(!CELL(1, 1));
    CHECK(CELL(0, 2));   /* Last line, before the end. */
    CHECK(!CELL(2, 2));  /* After the end. */

    /* The highlight stays out of the tree when scrolled sideways. */
    click_row("s.txt");
    char line[400] = "";
    memset(line, 'x', 300);
    click(col_x(0), row_y(0));
    SDL_SetClipboardText(line);
    key_mod(SDLK_V, SDL_KMOD_GUI);
    CHECK(EDITOR.left > 0);
    key_mod(SDLK_LEFT, SDL_KMOD_GUI | SDL_KMOD_SHIFT);
    CHECK(EDITOR.left == 0);
    key_shift(SDLK_END);
    CHECK(EDITOR.left > 0);
    click_tree_empty(); /* The tree's own highlight would hide a leak. */
    CHECK(selected_is("abc"));
    SDL_Rect t = app_tree_rect(&app);
    CHECK(pixel_is(t.x + t.w - 2, t.y + h / 2, theme_current()->sidebar));

    /* Clearing the selection removes the highlight. */
    click(col_x(0), row_y(1));
    mouse_up();
    CHECK(pixel_is(e.x + 1, e.y + 2 * h + 1, background));
#undef CELL
    end_test();
}

static void wheel_sideways(int x, int y, int ticks) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.integer_x = ticks;
    event.wheel.mouse_x = (float)x;
    event.wheel.mouse_y = (float)y;
    send(&event);
}

/* Open a folder, given relative to where the test started. */
static char test_base[1024];

static void open_folder_named(const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", test_base, name);
    app_open_folder(&app, path);
}

static int current_folder_ends_with(const char *name) {
    char folder[1024];
    if (getcwd(folder, sizeof(folder)) == NULL) return 0;
    size_t len = strlen(folder), n = strlen(name);
    return len >= n && strcmp(folder + len - n, name) == 0;
}

static void test_open_folder(void) {
    start_test("folders2");
    CHECK(getcwd(test_base, sizeof(test_base)) != NULL);
    CHECK(fs_create_folder("one") == 0);
    CHECK(fs_create_folder("one/sub") == 0);
    CHECK(fs_write_file("one/sub/deep.txt", "d", 1) == 0);
    CHECK(fs_write_file("one/a.txt", "a", 1) == 0);
    CHECK(fs_write_file("one/b.txt", "b", 1) == 0);
    CHECK(fs_create_folder("two") == 0);
    CHECK(fs_write_file("two/c.txt", "c", 1) == 0);
    CHECK(fs_create_folder("two/sub") == 0);
    CHECK(fs_write_file("two/sub/other.txt", "o", 1) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);

    /* The button and Cmd-O both ask for the picker, and nothing else changes. */
    CHECK(app.wants_folder_dialog == 0);
    click_button(BUTTON_OPEN_FOLDER);
    CHECK(app.wants_folder_dialog == 1);
    app.wants_folder_dialog = 0;
    key_mod(SDLK_O, SDL_KMOD_GUI);
    CHECK(app.wants_folder_dialog == 1);
    app.wants_folder_dialog = 0;
    key_mod(SDLK_O, SDL_KMOD_CTRL);
    CHECK(app.wants_folder_dialog == 1);
    app.wants_folder_dialog = 0;
    CHECK(current_folder_ends_with("/folders2"));

    /* They do nothing while a question is waiting or the menu is open. */
    menu_choose(MENU_NEW_FILE);
    CHECK(app.mode == MODE_PROMPT);
    click_button(BUTTON_OPEN_FOLDER);
    key_mod(SDLK_O, SDL_KMOD_GUI);
    CHECK(app.wants_folder_dialog == 0);
    key(SDLK_ESCAPE);
    right_click_tree_empty();
    click_button(BUTTON_OPEN_FOLDER);
    CHECK(app.mode == MODE_EDIT);
    CHECK(app.wants_folder_dialog == 0);

    /* Opening a folder shows its tree from the top, with nothing carried over. */
    open_folder_named("one");
    CHECK(current_folder_ends_with("/one"));
    CHECK(strstr(app.message, "Opened") != NULL);
    CHECK(tree_find(&app.tree, "a.txt") >= 0);
    CHECK(tree_find(&app.tree, "two") == -1);
    click_row("sub");
    click_row("sub/deep.txt");
    click_row("a.txt");
    CHECK(app.tab_count == 2);

    /* Without unsaved work the tabs just close. A folder expanded by the same
     * name stays closed, and scrolling starts over. */
    SDL_Rect t = app_tree_rect(&app);
    app.tree_top = 2;
    app.tree_left = 1;
    open_folder_named("two");
    CHECK(current_folder_ends_with("/two"));
    CHECK(app.tab_count == 0);
    CHECK(app.selected == NULL);
    CHECK(app.tree_top == 0 && app.tree_left == 0);
    CHECK(tree_find(&app.tree, "sub") >= 0);
    CHECK(tree_find(&app.tree, "sub/other.txt") == -1);
    CHECK(tree_find(&app.tree, "c.txt") >= 0);
    CHECK(t.w > 0);

    /* The current folder again, by any spelling, keeps the tabs. */
    click_row("c.txt");
    app_open_folder(&app, ".");
    CHECK(strstr(app.message, "already open") != NULL);
    CHECK(app.tab_count == 1);
    open_folder_named("two");
    CHECK(strstr(app.message, "already open") != NULL);
    CHECK(app.tab_count == 1);

    /* A folder that cannot be opened changes nothing. */
    open_folder_named("missing");
    CHECK(strstr(app.message, "Cannot open") != NULL);
    CHECK(current_folder_ends_with("/two"));
    CHECK(app.tab_count == 1);
    CHECK(fs_create_folder("locked") == 0);
    CHECK(chmod("locked", 0) == 0);
    app_open_folder(&app, "locked");
    CHECK(strstr(app.message, "Cannot open") != NULL);
    CHECK(app.tab_count == 1);
    CHECK(chmod("locked", 0755) == 0);
    CHECK(fs_delete("locked") == 0);

    /* Unsaved work is asked about first. Esc cancels, staying where we are. */
    text("X");
    open_folder_named("one");
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(strstr(app.question, "c.txt") != NULL);
    CHECK(current_folder_ends_with("/two"));
    key(SDLK_ESCAPE);
    CHECK(app.mode == MODE_EDIT);
    CHECK(app.pending_folder == NULL);
    CHECK(current_folder_ends_with("/two"));
    CHECK(EDITOR.buffer.dirty == 1);

    /* y saves into the old folder, before the switch. */
    open_folder_named("one");
    press_letter(SDLK_Y, "y");
    CHECK(current_folder_ends_with("/one"));
    CHECK(app.tab_count == 0);
    CHECK(app.mode == MODE_EDIT);
    char saved[1200];
    snprintf(saved, sizeof(saved), "%s/two/c.txt", test_base);
    CHECK(file_is(saved, "Xc"));

    /* n discards, and every unsaved tab gets its turn. */
    click_row("a.txt");
    text("1");
    click_row("b.txt");
    text("2");
    open_folder_named("two");
    CHECK(strstr(app.question, "a.txt") != NULL);
    press_letter(SDLK_N, "n");
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(strstr(app.question, "b.txt") != NULL);
    CHECK(current_folder_ends_with("/one"));
    press_letter(SDLK_Y, "y");
    CHECK(current_folder_ends_with("/two"));
    CHECK(app.tab_count == 0);
    snprintf(saved, sizeof(saved), "%s/one/a.txt", test_base);
    CHECK(file_is(saved, "a"));
    snprintf(saved, sizeof(saved), "%s/one/b.txt", test_base);
    CHECK(file_is(saved, "2b"));

    /* A failed save stops the switch, with the tab and its changes intact. */
    click_row("c.txt");
    CHECK(chmod("c.txt", 0444) == 0);
    text("Y");
    open_folder_named("one");
    press_letter(SDLK_Y, "y");
    CHECK(strstr(app.message, "Save failed") != NULL);
    CHECK(current_folder_ends_with("/two"));
    CHECK(app.tab_count == 1);
    CHECK(EDITOR.buffer.dirty == 1);
    CHECK(app.pending_folder == NULL);
    CHECK(chmod("c.txt", 0644) == 0);

    /* Quitting while one is pending wins, and the switch is forgotten. */
    open_folder_named("one");
    CHECK(app.pending_folder != NULL);
    send_type(SDL_EVENT_QUIT);
    CHECK(app.pending_folder == NULL);
    CHECK(app.pending_quit == 1);
    press_letter(SDLK_N, "n");
    CHECK(app.running == 0);
    CHECK(current_folder_ends_with("/two"));

    /* Shutting down with a switch still pending leaves nothing behind. */
    app.running = 1;
    click_row("c.txt");
    text("Z");
    open_folder_named("one");
    CHECK(app.pending_folder != NULL);

    CHECK(chdir(test_base) == 0);
    end_test();
}

static void test_tree_scrolls_sideways(void) {
    start_test("sideways");
    SDL_Rect t = app_tree_rect(&app);
    int h = app.font.cell_h;
    Color sidebar = theme_current()->sidebar;

    /* Names that fit have nowhere to go. */
    CHECK(fs_create_file("short.txt") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    wheel_sideways(t.x + 1, t.y + 1, 5);
    CHECK(app.tree_left == 0);

    /* A long name, inside a folder so that its indent counts too. */
    char name[81];
    memset(name, 'n', 80);
    name[80] = '\0';
    char path[100];
    snprintf(path, sizeof(path), "dir/%s", name);
    CHECK(fs_create_folder("dir") == 0);
    CHECK(fs_create_file(path) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("dir");
    click_tree_empty();

    /* The row is cut off at the edge of the tree to begin with. */
    int row = tree_find(&app.tree, path);
    int y = t.y + row * h + h / 2;
    CHECK(area_has_ink(t.x + t.w - app.font.cell_w, t.y + row * h, app.font.cell_w, h, sidebar));

    /* It scrolls in steps, and stops once the end of the name is in view. */
    wheel_sideways(t.x + 1, y, 1);
    CHECK(app.tree_left == 3);
    wheel_sideways(t.x + 1, y, 1000);
    int expected = 2 + 3 + 80 + 1 - t.w / app.font.cell_w;
    CHECK(app.tree_left == expected);

    /* The last letter is on screen, with a little space after it. */
    int end_x = t.x + (2 + 3 + 80 - app.tree_left) * app.font.cell_w;
    CHECK(end_x <= t.x + t.w);
    CHECK(area_has_ink(end_x - app.font.cell_w, t.y + row * h, app.font.cell_w, h, sidebar));
    CHECK(!area_has_ink(end_x, t.y + row * h, t.x + t.w - end_x, h, sidebar));

    /* Text stays inside the tree, leaving the text area clear. */
    SDL_Rect e = app_editor_rect(&app);
    CHECK(!area_has_ink(t.x + t.w, e.y, e.x - t.x - t.w, h, theme_current()->background));

    /* Clicks still land on the right rows. */
    click_row(path);
    CHECK(strcmp(EDITOR.path, path) == 0);
    click_row("short.txt");
    CHECK(strcmp(EDITOR.path, "short.txt") == 0);
    CHECK(app.tree_left == expected);

    /* Vertical scrolling leaves it alone, and it stops at the other end too. */
    wheel(t.x + 1, y, -1);
    CHECK(app.tree_left == expected);
    wheel(t.x + 1, y, 1);
    wheel_sideways(t.x + 1, y, -1000);
    CHECK(app.tree_left == 0);

    /* The wheel over the text area does not move the tree. */
    wheel_sideways(e.x + 1, e.y + 1, 5);
    CHECK(app.tree_left == 0);

    /* Closing the folder removes the long row, so there is nothing left to scroll. */
    wheel_sideways(t.x + 1, y, 1000);
    CHECK(app.tree_left > 0);
    click_row("dir");
    CHECK(tree_find(&app.tree, path) == -1);
    CHECK(app.tree_left == 0);
    end_test();
}

static void test_font_size(void) {
    start_test("fontsize");
    char big[2000] = "";
    for (int i = 0; i < 100; i++) strcat(big, "row\n");
    CHECK(fs_write_file("big.txt", big, strlen(big)) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("big.txt");
    int original = app.font_size;
    int cell_w = app.font.cell_w;
    int cell_h = app.font.cell_h;
    int rows = app_editor_rect(&app).h / cell_h;

    /* The same size changes nothing, and does not reload the font. */
    SDL_Texture *atlas = app.font.atlas;
    app_set_font_size(&app, original);
    CHECK(app.font.atlas == atlas);

    /* Put the cursor on the last visible row, then double the text size, as when
     * the window moves to a screen with twice the pixels per point. */
    click(col_x(0), row_y(rows - 1));
    mouse_up();
    CHECK(EDITOR.row == rows - 1);
    app_set_font_size(&app, original * 2);
    app_draw(&app);
    SDL_RenderPresent(app.renderer);
    CHECK(app.font_size == original * 2);
    CHECK(app.font.cell_w > cell_w);
    CHECK(app.font.cell_h > cell_h);

    /* Fewer rows fit now, and the cursor is still on one of them. */
    int fewer = app_editor_rect(&app).h / app.font.cell_h;
    CHECK(fewer < rows);
    CHECK(EDITOR.row >= EDITOR.top && EDITOR.row < EDITOR.top + fewer);
    CHECK(area_has_ink(app_editor_rect(&app).x, app_editor_rect(&app).y, app.font.cell_w * 3,
                       app.font.cell_h, theme_current()->background));

    /* Clicks use the new size. */
    int top = EDITOR.top;
    click(app_editor_rect(&app).x + app.font.cell_w * 2 + 1, app_editor_rect(&app).y + app.font.cell_h + 1);
    mouse_up();
    CHECK(EDITOR.row == top + 1 && EDITOR.col == 2);
    key_mod(SDLK_W, SDL_KMOD_GUI);
    CHECK(app.tab_count == 0);
    click_row("big.txt");
    CHECK(strcmp(EDITOR.path, "big.txt") == 0);

    /* An open menu would be in the wrong place, so it closes. */
    right_click_row("big.txt");
    CHECK(app.mode == MODE_MENU);
    app_set_font_size(&app, original);
    CHECK(app.mode == MODE_EDIT);
    CHECK(app.font.cell_w == cell_w && app.font.cell_h == cell_h);

    /* A question stays open, since nothing about it depends on the size. */
    menu_choose(MENU_RENAME);
    CHECK(app.mode == MODE_PROMPT);
    app_set_font_size(&app, original * 2);
    CHECK(app.mode == MODE_PROMPT);
    app_set_font_size(&app, original);
    key(SDLK_ESCAPE);
    end_test();
}

static void click_answer(Answer answer) {
    SDL_Rect r = app_answer_rect(&app, answer);
    CHECK(r.w > 0);
    click(r.x + 1, r.y + 1);
}

static void test_question_window(void) {
    start_test("question");
    CHECK(fs_write_file("a.txt", "a", 1) == 0);
    CHECK(fs_write_file("b.txt", "b", 1) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    SDL_Rect t = app_tree_rect(&app);
    Color sidebar = theme_current()->sidebar;
    CHECK(pixel_is(t.x + 1, t.y + t.h - 2, sidebar));

    /* It is in the middle of the window, over everything else, which is dimmed. */
    click_row("a.txt");
    menu_choose(MENU_DELETE);
    CHECK(app.mode == MODE_CONFIRM);
    SDL_Rect box = app_question_rect(&app);
    CHECK(box.x > 0 && box.x + box.w < 1000 && box.y > 0 && box.y + box.h < 700);
    CHECK(abs(box.x - (1000 - box.x - box.w)) <= 1);
    CHECK(abs(box.y - (700 - box.y - box.h)) <= 1);
    CHECK(pixel_is(box.x + box.w / 2, box.y + 2, theme_current()->bar));
    CHECK(area_has_ink(box.x + app.font.cell_w * 2, box.y + app.font.cell_h, app.font.cell_w * 8,
                       app.font.cell_h, theme_current()->bar));
    CHECK(!pixel_is(t.x + 1, t.y + t.h - 2, sidebar));

    /* Deleting offers two buttons, inside the window. */
    SDL_Rect yes = app_answer_rect(&app, ANSWER_YES);
    SDL_Rect cancel = app_answer_rect(&app, ANSWER_CANCEL);
    CHECK(app_answer_rect(&app, ANSWER_NO).w == 0);
    CHECK(yes.x >= box.x && yes.x + yes.w <= cancel.x);
    CHECK(cancel.x + cancel.w <= box.x + box.w && cancel.y + cancel.h <= box.y + box.h);

    /* A click anywhere else does nothing, and does not change what gets deleted. */
    click_row("b.txt");
    click(box.x + 1, box.y + 1);
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(strcmp(app.selected, "a.txt") == 0);

    /* Cancel keeps the file, and the dimming goes. */
    click_answer(ANSWER_CANCEL);
    CHECK(app.mode == MODE_EDIT);
    CHECK(access("a.txt", F_OK) == 0);
    CHECK(pixel_is(t.x + 1, t.y + t.h - 2, sidebar));

    /* Delete deletes. */
    menu_choose(MENU_DELETE);
    click_answer(ANSWER_YES);
    CHECK(app.mode == MODE_EDIT);
    CHECK(access("a.txt", F_OK) != 0);
    CHECK(app.tab_count == 0);

    /* A name: OK needs one, and takes what was typed. */
    click_tree_empty();
    menu_choose(MENU_NEW_FILE);
    CHECK(app.mode == MODE_PROMPT);
    CHECK(app_answer_rect(&app, ANSWER_NO).w == 0);
    click_answer(ANSWER_YES);
    CHECK(app.mode == MODE_PROMPT);
    text("c.txt");
    click_answer(ANSWER_YES);
    CHECK(app.mode == MODE_EDIT);
    CHECK(access("c.txt", F_OK) == 0);

    /* Cancel drops the name. Clicks elsewhere leave it being typed. */
    click_tree_empty();
    menu_choose(MENU_NEW_FOLDER);
    text("nope");
    click_row("b.txt");
    CHECK(app.mode == MODE_PROMPT);
    click_answer(ANSWER_CANCEL);
    CHECK(app.mode == MODE_EDIT);
    CHECK(access("nope", F_OK) != 0);

    /* Unsaved changes offer all three. Cancel keeps the tab. */
    click_row("b.txt");
    text("B");
    click_tab_close("b.txt");
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(app_answer_rect(&app, ANSWER_NO).w > 0);
    click_answer(ANSWER_CANCEL);
    CHECK(tab_index("b.txt") >= 0);
    CHECK(EDITOR.buffer.dirty == 1);

    /* Save writes the file and closes the tab. */
    click_tab_close("b.txt");
    click_answer(ANSWER_YES);
    CHECK(tab_index("b.txt") == -1);
    CHECK(file_is("b.txt", "Bb"));

    /* Discard leaves the file alone, and works when quitting too. */
    click_row("b.txt");
    text("X");
    send_type(SDL_EVENT_QUIT);
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(app.running == 1);
    click_answer(ANSWER_NO);
    CHECK(app.running == 0);
    CHECK(file_is("b.txt", "Bb"));
    app.running = 1;

    /* A long name wraps, and everything stays inside the window. */
    char name[201];
    memset(name, 'n', 200);
    name[200] = '\0';
    CHECK(fs_write_file(name, "", 0) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row(name);
    menu_choose(MENU_RENAME);
    CHECK(app.mode == MODE_PROMPT);
    SDL_Rect tall = app_question_rect(&app);
    SDL_Rect ok = app_answer_rect(&app, ANSWER_YES);
    CHECK(tall.h > box.h + app.font.cell_h * 2);
    CHECK(tall.x + tall.w <= 1000 && tall.y + tall.h <= 700);
    CHECK(ok.y > tall.y + app.font.cell_h * 4 && ok.y + ok.h <= tall.y + tall.h);
    click_answer(ANSWER_CANCEL);
    CHECK(access(name, F_OK) == 0);

    end_test();
}

static void test_themes_and_drawing(void) {
    start_test("draw");
    SDL_Rect e = app_editor_rect(&app);
    SDL_Rect t = app_tree_rect(&app);

    CHECK(strcmp(theme_current()->name, "dark") == 0);
    CHECK(fs_write_file("t.txt", "text", 4) == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("t.txt");

    Color dark_background = theme_current()->background;
    CHECK(pixel_is(e.x + e.w - 2, e.y + e.h - 2, dark_background));
    CHECK(pixel_is(t.x + t.w - 2, t.y + t.h - 2, theme_current()->sidebar));

    /* The text, its cursor and the tree are really drawn. */
    CHECK(area_has_ink(e.x, e.y, app.font.cell_w * 4, app.font.cell_h, dark_background));

    click_button(BUTTON_THEME);
    CHECK(strcmp(theme_current()->name, "light") == 0);
    CHECK(pixel_is(e.x + e.w - 2, e.y + e.h - 2, theme_current()->background));
    CHECK(pixel_is(t.x + t.w - 2, t.y + t.h - 2, theme_current()->sidebar));
    CHECK(area_has_ink(e.x, e.y, app.font.cell_w * 4, app.font.cell_h, theme_current()->background));
    CHECK(!pixel_is(e.x + e.w - 2, e.y + e.h - 2, dark_background));

    click_button(BUTTON_THEME);
    CHECK(strcmp(theme_current()->name, "dark") == 0);
    CHECK(pixel_is(e.x + e.w - 2, e.y + e.h - 2, dark_background));
    end_test();
}

int main(void) {
    char folder[1024];
    enter_temp_folder(folder, sizeof(folder));

    /* The dummy driver has its own clipboard, so tests leave the real one alone. */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO) || strcmp(SDL_GetCurrentVideoDriver(), "dummy") != 0) {
        printf("could not start the dummy video driver: %s\n", SDL_GetError());
        return 1;
    }

    surface = SDL_CreateSurface(1000, 700, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *renderer = surface != NULL ? SDL_CreateSoftwareRenderer(surface) : NULL;
    if (renderer == NULL || app_init(&app, renderer, 14) != 0) {
        printf("could not set up: %s\n", SDL_GetError());
        return 1;
    }

    test_create_open_edit_save();
    test_prompts();
    test_tree_and_folders();
    test_rename();
    test_delete();
    test_tabs();
    test_closing_unsaved_tabs();
    test_question_window();
    test_tabs_follow_files();
    test_many_tabs();
    test_right_click_menu();
    test_open_folder();
    test_tree_scrolls_sideways();
    test_font_size();
    test_layout();
    test_quit();
    test_non_english_input_and_exact_saves();
    test_mouse_in_editor();
    test_scrolling();
    test_mouse_selection();
    test_copy_and_paste();
    test_undo();
    test_selection_is_drawn();
    test_themes_and_drawing();

    app_free(&app);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    SDL_Quit();
    return finish(folder);
}
