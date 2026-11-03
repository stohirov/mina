/* Tests for the application. They send it the same events a window would, and
 * draw to an in-memory surface after each one. */
#include <SDL3/SDL.h>
#include <sys/stat.h>

#include "app.h"
#include "check.h"
#include "theme.h"

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

static void new_item(Button button, const char *name) {
    click_button(button);
    text(name);
    key(SDLK_RETURN);
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
    CHECK(!app.has_file);
    CHECK(strstr(app.message, "Open a file") != NULL);

    /* A new file opens straight away. */
    new_item(BUTTON_NEW_FILE, "note.txt");
    CHECK(file_is("note.txt", ""));
    CHECK(app.has_file);
    CHECK(strcmp(app.editor.path, "note.txt") == 0);
    CHECK(strcmp(app.selected, "note.txt") == 0);

    text("line one");
    key(SDLK_RETURN);
    text("line two");
    CHECK(app.editor.buffer.dirty == 1);
    CHECK(file_is("note.txt", ""));

    key_mod(SDLK_S, SDL_KMOD_GUI);
    CHECK(file_is("note.txt", "line one\nline two"));
    CHECK(app.editor.buffer.dirty == 0);
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
    CHECK(app.editor.row == 0 && app.editor.col == 8);
    key_mod(SDLK_LEFT, SDL_KMOD_GUI);
    CHECK(app.editor.col == 0);
    key_mod(SDLK_RIGHT, SDL_KMOD_GUI);
    CHECK(app.editor.col == 8);

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
    click_button(BUTTON_NEW_FILE);
    CHECK(app.mode == MODE_PROMPT);
    text("nope");
    key(SDLK_ESCAPE);
    CHECK(app.mode == MODE_EDIT);
    CHECK(access("nope", F_OK) != 0);

    /* Backspace works, and Enter with no name does nothing. */
    click_button(BUTTON_NEW_FILE);
    key(SDLK_RETURN);
    CHECK(app.mode == MODE_PROMPT);
    text("abX");
    key(SDLK_BACKSPACE);
    key(SDLK_BACKSPACE);
    text("c");
    key(SDLK_RETURN);
    CHECK(access("ac", F_OK) == 0);

    /* Names holding a path are refused. */
    click_button(BUTTON_NEW_FILE);
    text("../escape");
    key(SDLK_RETURN);
    CHECK(strstr(app.message, "Invalid name") != NULL);
    CHECK(app.mode == MODE_EDIT);
    CHECK(access("../escape", F_OK) != 0);

    /* Creating over an existing name fails and keeps the file. */
    CHECK(fs_write_file("ac", "keep", 4) == 0);
    new_item(BUTTON_NEW_FILE, "ac");
    CHECK(strstr(app.message, "File exists") != NULL);
    CHECK(file_is("ac", "keep"));

    /* While a prompt is open, buttons and the tree do nothing. */
    click_button(BUTTON_NEW_FOLDER);
    click_button(BUTTON_DELETE);
    CHECK(app.action == ACTION_NEW_FOLDER);
    key(SDLK_ESCAPE);

    /* A long name cannot overflow the input. */
    click_button(BUTTON_NEW_FILE);
    for (int i = 0; i < 400; i++) text("a");
    CHECK(strlen(app.input) == sizeof(app.input) - 1);
    key(SDLK_ESCAPE);

    end_test();
}

static void test_tree_and_folders(void) {
    start_test("folders");

    /* Nothing selected: a new folder goes at the top. */
    new_item(BUTTON_NEW_FOLDER, "src");
    CHECK(access("src", F_OK) == 0);
    CHECK(strcmp(app.selected, "src") == 0);
    CHECK(!app.has_file);

    /* The selected folder receives new items and opens to show them. */
    new_item(BUTTON_NEW_FILE, "main.c");
    CHECK(access("src/main.c", F_OK) == 0);
    CHECK(tree_find(&app.tree, "src/main.c") >= 0);
    CHECK(strcmp(app.editor.path, "src/main.c") == 0);

    /* The selected file's folder receives new items too. */
    new_item(BUTTON_NEW_FOLDER, "sub");
    CHECK(access("src/sub", F_OK) == 0);

    click_tree_empty();
    CHECK(app.selected == NULL);
    new_item(BUTTON_NEW_FILE, "top.txt");
    CHECK(access("top.txt", F_OK) == 0);

    /* Clicking a folder closes it, and clicking again opens it. */
    CHECK(tree_find(&app.tree, "src/main.c") >= 0);
    click_row("src");
    CHECK(tree_find(&app.tree, "src/main.c") == -1);
    click_row("src");
    CHECK(tree_find(&app.tree, "src/main.c") >= 0);

    /* Clicking a file opens it. */
    click_row("src/main.c");
    CHECK(strcmp(app.editor.path, "src/main.c") == 0);

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
    CHECK(strcmp(app.editor.path, "src/main.c") == 0);

    end_test();
}

static void test_rename(void) {
    start_test("rename");
    click_button(BUTTON_RENAME);
    CHECK(strstr(app.message, "Select a file") != NULL);
    CHECK(app.mode == MODE_EDIT);

    new_item(BUTTON_NEW_FILE, "a.txt");
    click_tree_empty();
    new_item(BUTTON_NEW_FILE, "b.txt");
    CHECK(fs_write_file("a.txt", "A", 1) == 0);

    /* The prompt starts with the current name. */
    click_button(BUTTON_RENAME);
    CHECK(strcmp(app.input, "b.txt") == 0);

    /* Refuses to overwrite another file. */
    for (int i = 0; i < 5; i++) key(SDLK_BACKSPACE);
    text("a.txt");
    key(SDLK_RETURN);
    CHECK(strstr(app.message, "File exists") != NULL);
    CHECK(file_is("a.txt", "A"));

    /* Renaming the open file makes later saves go to the new name. */
    text("hello");
    click_button(BUTTON_RENAME);
    for (int i = 0; i < 5; i++) key(SDLK_BACKSPACE);
    text("c.txt");
    key(SDLK_RETURN);
    CHECK(access("b.txt", F_OK) != 0);
    CHECK(strcmp(app.editor.path, "c.txt") == 0);
    CHECK(strcmp(app.selected, "c.txt") == 0);
    key_mod(SDLK_S, SDL_KMOD_GUI);
    CHECK(file_is("c.txt", "hello"));
    CHECK(access("b.txt", F_OK) != 0);

    /* Renaming a folder updates the path of an open file inside it. */
    click_tree_empty();
    new_item(BUTTON_NEW_FOLDER, "dir");
    new_item(BUTTON_NEW_FILE, "in.txt");
    click_row("dir");
    click_button(BUTTON_RENAME);
    for (int i = 0; i < 3; i++) key(SDLK_BACKSPACE);
    text("box");
    key(SDLK_RETURN);
    CHECK(access("box/in.txt", F_OK) == 0);
    CHECK(strcmp(app.editor.path, "box/in.txt") == 0);
    text("z");
    key_mod(SDLK_S, SDL_KMOD_GUI);
    CHECK(file_is("box/in.txt", "z"));

    /* A folder whose name is only the start of "box" is a different folder. */
    click_tree_empty();
    new_item(BUTTON_NEW_FOLDER, "bo");
    click_button(BUTTON_RENAME);
    for (int i = 0; i < 2; i++) key(SDLK_BACKSPACE);
    text("other");
    key(SDLK_RETURN);
    CHECK(access("other", F_OK) == 0);
    CHECK(strcmp(app.editor.path, "box/in.txt") == 0);

    end_test();
}

static void test_delete(void) {
    start_test("delete");
    click_button(BUTTON_DELETE);
    CHECK(strstr(app.message, "Select a file") != NULL);
    CHECK(app.mode == MODE_EDIT);

    new_item(BUTTON_NEW_FOLDER, "box");
    new_item(BUTTON_NEW_FILE, "in.txt");
    click_tree_empty();
    new_item(BUTTON_NEW_FILE, "keep.txt");
    new_item(BUTTON_NEW_FILE, "gone.txt");

    /* n and Esc keep the file. */
    click_button(BUTTON_DELETE);
    CHECK(app.mode == MODE_CONFIRM);
    press_letter(SDLK_N, "n");
    CHECK(access("gone.txt", F_OK) == 0);
    CHECK(app.mode == MODE_EDIT);
    CHECK(app.editor.buffer.dirty == 0); /* The n was not typed into the file. */

    click_button(BUTTON_DELETE);
    key(SDLK_ESCAPE);
    CHECK(access("gone.txt", F_OK) == 0);

    /* Other keys do not answer the question. */
    click_button(BUTTON_DELETE);
    press_letter(SDLK_X, "x");
    key(SDLK_RETURN);
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(access("gone.txt", F_OK) == 0);

    /* y deletes the open file and closes it. */
    press_letter(SDLK_Y, "y");
    CHECK(access("gone.txt", F_OK) != 0);
    CHECK(!app.has_file);
    CHECK(app.selected == NULL);
    CHECK(tree_find(&app.tree, "gone.txt") == -1);

    /* Deleting a folder deletes what is inside and closes a file open there. */
    click_row("box/in.txt");
    CHECK(app.has_file);
    click_row("box");
    click_button(BUTTON_DELETE);
    CHECK(strstr(app.question, "everything in it") != NULL);
    press_letter(SDLK_Y, "y");
    CHECK(access("box", F_OK) != 0);
    CHECK(!app.has_file);

    /* Deleting something else leaves the open file open. */
    click_row("keep.txt");
    CHECK(fs_create_file("other.txt") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    text("unsaved");
    click_tree_empty();
    CHECK(fs_create_folder("d") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("d");
    click_button(BUTTON_DELETE);
    press_letter(SDLK_Y, "y");
    CHECK(app.has_file);
    CHECK(strcmp(app.editor.path, "keep.txt") == 0);
    CHECK(app.editor.buffer.dirty == 1);
    CHECK(app.editor.buffer.lines[0].len == 7);

    /* A link is removed without touching its target. */
    CHECK(fs_create_folder("precious") == 0);
    CHECK(fs_create_file("precious/file") == 0);
    CHECK(symlink("precious", "link") == 0);
    send_type(SDL_EVENT_WINDOW_FOCUS_GAINED);
    click_row("link");
    click_button(BUTTON_DELETE);
    press_letter(SDLK_Y, "y");
    CHECK(access("precious/file", F_OK) == 0);

    end_test();
}

static void test_unsaved_changes(void) {
    start_test("unsaved");
    new_item(BUTTON_NEW_FILE, "a.txt");
    click_tree_empty();
    new_item(BUTTON_NEW_FILE, "b.txt");
    click_row("a.txt");
    text("A");

    /* Clicking the same file again asks nothing. */
    click_row("a.txt");
    CHECK(app.mode == MODE_EDIT);

    /* Esc stays in the file with the changes kept. */
    click_row("b.txt");
    CHECK(app.mode == MODE_CONFIRM);
    CHECK(strstr(app.question, "Save changes to a.txt") != NULL);
    key(SDLK_ESCAPE);
    CHECK(strcmp(app.editor.path, "a.txt") == 0);
    CHECK(app.editor.buffer.dirty == 1);
    CHECK(app.pending_open == NULL);

    /* n discards. The answer must not be typed into the next file. */
    click_row("b.txt");
    press_letter(SDLK_N, "n");
    CHECK(strcmp(app.editor.path, "b.txt") == 0);
    CHECK(app.editor.buffer.dirty == 0);
    CHECK(app.editor.buffer.lines[0].len == 0);
    CHECK(file_is("a.txt", ""));

    /* y saves and moves on, again without typing the answer. */
    click_row("a.txt");
    text("A");
    click_row("b.txt");
    press_letter(SDLK_Y, "y");
    CHECK(file_is("a.txt", "A"));
    CHECK(strcmp(app.editor.path, "b.txt") == 0);
    CHECK(app.editor.buffer.dirty == 0);
    CHECK(app.editor.buffer.lines[0].len == 0);

    /* A failed save keeps the file open with its changes. */
    CHECK(chmod("b.txt", 0444) == 0);
    text("B");
    click_row("a.txt");
    press_letter(SDLK_Y, "y");
    CHECK(strstr(app.message, "Save failed") != NULL);
    CHECK(strcmp(app.editor.path, "b.txt") == 0);
    CHECK(app.editor.buffer.dirty == 1);
    CHECK(app.mode == MODE_EDIT);
    CHECK(file_is("b.txt", ""));
    CHECK(chmod("b.txt", 0644) == 0);

    /* Quitting asks too. */
    send_type(SDL_EVENT_QUIT);
    CHECK(app.running == 1);
    CHECK(app.mode == MODE_CONFIRM);
    key(SDLK_ESCAPE);
    CHECK(app.running == 1);
    CHECK(app.pending_quit == 0);

    key_mod(SDLK_Q, SDL_KMOD_GUI);
    CHECK(app.mode == MODE_CONFIRM);
    press_letter(SDLK_Y, "y");
    CHECK(app.running == 0);
    CHECK(file_is("b.txt", "B"));

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
    CHECK(app.editor.buffer.dirty == 0);
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
    CHECK(app.editor.row == 0 && app.editor.col == 2);
    click(r.x + w * 5 + 1, r.y + h * 1 + 1); /* After the tab. */
    CHECK(app.editor.row == 1 && app.editor.col == 2);
    click(r.x + w * 50, r.y + h * 2 + 1);
    CHECK(app.editor.row == 2 && app.editor.col == 5);
    click(r.x + 1, r.y + r.h - 1); /* Below the last line. */
    CHECK(app.editor.row == 2 && app.editor.col == 0);
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
    int last = app.editor.buffer.count - 1;
    CHECK(last == 100);

    key(SDLK_PAGEDOWN);
    CHECK(app.editor.row == rows);
    CHECK(app.editor.top == 1);
    for (int i = 0; i < 20; i++) key(SDLK_PAGEDOWN);
    CHECK(app.editor.row == last);
    CHECK(app.editor.top == last - rows + 1);

    /* The wheel scrolls without moving the cursor, and stops at both ends. */
    int top = app.editor.top;
    wheel(e.x + 1, e.y + 1, 1);
    CHECK(app.editor.top < top);
    CHECK(app.editor.row == last);
    wheel(e.x + 1, e.y + 1, 1000);
    CHECK(app.editor.top == 0);
    wheel(e.x + 1, e.y + 1, -1000);
    CHECK(app.editor.top == last);
    wheel(e.x + 1, e.y + 1, 1000);

    /* Typing brings the cursor back into view. */
    text("x");
    CHECK(app.editor.top == last - rows + 1);

    /* A long line scrolls sideways, and the text stays inside its area. */
    int cols = e.w / app.font.cell_w;
    for (int i = 0; i < cols + 5; i++) text("y");
    CHECK(app.editor.left > 0);
    key(SDLK_HOME);
    CHECK(app.editor.left == 0);

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
    CHECK(strcmp(app.editor.path, "f050") == 0);
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
    char *selected = editor_selected_text(&app.editor);
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
    CHECK(app.editor.top > 0);
    CHECK(app.editor.selecting);
    int top = app.editor.top;
    for (int i = 0; i < 3; i++) mouse_motion(col_x(0), e.y - 5);
    CHECK(app.editor.top < top);
    CHECK(app.editor.row == app.editor.top);
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
    CHECK(!app.has_file);
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
    CHECK(app.editor.buffer.dirty == 0);
    CHECK(selected_is("hello"));

    /* Ctrl works as well as Cmd. */
    key(SDLK_END);
    key_mod(SDLK_V, SDL_KMOD_CTRL);
    click_button(BUTTON_SAVE);
    CHECK(file_is("c.txt", "hello worldhello\nsecond"));
    CHECK(app.editor.col == 16);

    /* Pasting over a selection replaces it, and can span lines. */
    SDL_SetClipboardText("one\ntwo");
    key(SDLK_HOME);
    key_shift(SDLK_DOWN);
    key_mod(SDLK_V, SDL_KMOD_GUI);
    click_button(BUTTON_SAVE);
    CHECK(file_is("c.txt", "one\ntwosecond"));
    CHECK(app.editor.row == 1 && app.editor.col == 3);

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
    CHECK(app.editor.top > 0);
    int rows = app_editor_rect(&app).h / app.font.cell_h;
    CHECK(app.editor.row < app.editor.top + rows);

    /* One Cmd-Z removes the whole paste. */
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(app.editor.buffer.count == 2);
    CHECK(app.editor.buffer.dirty == 0);

    /* Non-English text pasted in keeps its bytes, like the rest of a file. */
    SDL_SetClipboardText("\xc3\xa9");
    key_mod(SDLK_V, SDL_KMOD_GUI);
    click_button(BUTTON_SAVE);
    CHECK(strstr(app.editor.buffer.lines[0].text, "\xc3\xa9") != NULL);

    /* The clipboard outlives the file it was copied from. */
    SDL_SetClipboardText("across");
    click_row("other.txt");
    CHECK(strcmp(app.editor.path, "other.txt") == 0);
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
    CHECK(app.editor.buffer.dirty == 1);
    CHECK(app.editor.col == 0);

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
    CHECK(app.editor.buffer.dirty == 0);
    click_row("other.txt");
    CHECK(app.mode == MODE_EDIT);
    CHECK(strcmp(app.editor.path, "other.txt") == 0);

    /* Each file has its own history. */
    key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(strstr(app.message, "Nothing to undo") != NULL);

    /* Undo scrolls back to the change. */
    for (int i = 0; i < 100; i++) key(SDLK_RETURN);
    CHECK(app.editor.top > 0);
    for (int i = 0; i < 100; i++) key_mod(SDLK_Z, SDL_KMOD_GUI);
    CHECK(app.editor.top == 0);
    CHECK(app.editor.buffer.dirty == 0);

    /* Cmd-Shift-Z is not undo. */
    text("k");
    key_mod(SDLK_Z, SDL_KMOD_GUI | SDL_KMOD_SHIFT);
    CHECK(app.editor.buffer.dirty == 1);
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
    CHECK(app.editor.left > 0);
    key_mod(SDLK_LEFT, SDL_KMOD_GUI | SDL_KMOD_SHIFT);
    CHECK(app.editor.left == 0);
    key_shift(SDLK_END);
    CHECK(app.editor.left > 0);
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
    test_unsaved_changes();
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
