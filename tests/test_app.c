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
    SDL_Rect save = app_button_rect(&app, BUTTON_SAVE);
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
    click(save.x - 1, first.y + 1);
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
    test_tabs_follow_files();
    test_many_tabs();
    test_right_click_menu();
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
