/* Tests for the text buffer, file operations, folder tree and editor logic. */
#include <errno.h>
#include <sys/stat.h>

#include "buffer.h"
#include "check.h"
#include "editor.h"
#include "tree.h"

static int exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

static int buffer_is(const Buffer *b, const char *expected) {
    size_t len;
    char *text = buffer_to_text(b, &len);
    int same = len == strlen(expected) && memcmp(text, expected, len) == 0;
    free(text);
    return same;
}

static void test_buffer_round_trip(void) {
    const char *samples[] = {"", "a", "a\n", "a\nb", "a\nb\n", "\n\n", "x\r\ny\r\n", "\ttab"};
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        Buffer b;
        buffer_init(&b, samples[i], strlen(samples[i]));
        CHECK(buffer_is(&b, samples[i]));
        CHECK(b.dirty == 0);
        buffer_free(&b);
    }
}

static void test_buffer_splits_lines(void) {
    Buffer b;
    buffer_init(&b, "one\ntwo\n", 8);
    CHECK(b.count == 3);
    CHECK(strcmp(b.lines[0].text, "one") == 0);
    CHECK(strcmp(b.lines[1].text, "two") == 0);
    CHECK(b.lines[2].len == 0);
    buffer_free(&b);

    buffer_init(&b, "", 0);
    CHECK(b.count == 1);
    CHECK(b.lines[0].len == 0);
    buffer_free(&b);
}

static void test_buffer_keeps_nul_bytes(void) {
    Buffer b;
    buffer_init(&b, "a\0b\nc", 5);
    size_t len;
    char *text = buffer_to_text(&b, &len);
    CHECK(len == 5);
    CHECK(memcmp(text, "a\0b\nc", 5) == 0);
    free(text);
    buffer_free(&b);
}

static Pos pos(int row, int col) {
    return (Pos){row, col};
}

/* Insert into "hello\nworld", check the result and the returned end position. */
static void check_insert(Pos at, const char *text, const char *expected, Pos end) {
    Buffer b;
    buffer_init(&b, "hello\nworld", 11);
    Pos got = buffer_insert_text(&b, at, text, strlen(text));
    CHECK(buffer_is(&b, expected));
    CHECK(got.row == end.row && got.col == end.col);
    CHECK(b.dirty == 1);
    for (int i = 0; i < b.count; i++) {
        CHECK((int)strlen(b.lines[i].text) == b.lines[i].len);
    }
    buffer_free(&b);
}

static void test_buffer_insert(void) {
    check_insert(pos(0, 0), "X", "Xhello\nworld", pos(0, 1));
    check_insert(pos(0, 2), "X", "heXllo\nworld", pos(0, 3));
    check_insert(pos(1, 5), "X", "hello\nworldX", pos(1, 6));
    check_insert(pos(0, 2), "\n", "he\nllo\nworld", pos(1, 0));
    check_insert(pos(1, 5), "\n", "hello\nworld\n", pos(2, 0));
    check_insert(pos(0, 2), "a\nb", "hea\nbllo\nworld", pos(1, 1));
    check_insert(pos(0, 2), "a\nb\nc\n", "hea\nb\nc\nllo\nworld", pos(3, 0));
    check_insert(pos(1, 0), "\n\n", "hello\n\n\nworld", pos(3, 0));
}

static void check_delete(Pos start, Pos end, const char *removed, const char *expected) {
    Buffer b;
    buffer_init(&b, "one\ntwo\nthree\nfour", 18);
    size_t len;
    char *text = buffer_text_range(&b, start, end, &len);
    CHECK(strcmp(text, removed) == 0);
    CHECK(len == strlen(removed));
    free(text);

    buffer_delete_range(&b, start, end);
    CHECK(buffer_is(&b, expected));
    CHECK(b.dirty == 1);
    for (int i = 0; i < b.count; i++) {
        CHECK((int)strlen(b.lines[i].text) == b.lines[i].len);
    }
    buffer_free(&b);
}

static void test_buffer_ranges(void) {
    check_delete(pos(0, 1), pos(0, 2), "n", "oe\ntwo\nthree\nfour");
    check_delete(pos(0, 0), pos(0, 3), "one", "\ntwo\nthree\nfour");
    check_delete(pos(0, 3), pos(1, 0), "\n", "onetwo\nthree\nfour");
    check_delete(pos(0, 1), pos(1, 1), "ne\nt", "owo\nthree\nfour");
    check_delete(pos(0, 2), pos(2, 3), "e\ntwo\nthr", "onee\nfour");
    check_delete(pos(0, 0), pos(3, 4), "one\ntwo\nthree\nfour", "");
    check_delete(pos(2, 5), pos(3, 4), "\nfour", "one\ntwo\nthree");
}

/* Inserting back what was removed gives the original, for every possible range. */
static void test_buffer_delete_then_insert_restores(void) {
    const char *original = "ab\n\ncd\ne";
    Buffer b;
    buffer_init(&b, original, strlen(original));
    int count = b.count;
    int lens[] = {2, 0, 2, 1};
    buffer_free(&b);

    for (int r1 = 0; r1 < count; r1++) {
        for (int c1 = 0; c1 <= lens[r1]; c1++) {
            for (int r2 = r1; r2 < count; r2++) {
                for (int c2 = r2 == r1 ? c1 : 0; c2 <= lens[r2]; c2++) {
                    buffer_init(&b, original, strlen(original));
                    size_t len;
                    char *removed = buffer_text_range(&b, pos(r1, c1), pos(r2, c2), &len);
                    buffer_delete_range(&b, pos(r1, c1), pos(r2, c2));
                    Pos end = buffer_insert_text(&b, pos(r1, c1), removed, len);
                    CHECK(end.row == r2 && end.col == c2);
                    CHECK(buffer_is(&b, original));
                    free(removed);
                    buffer_free(&b);
                }
            }
        }
    }
}

static void test_create(void) {
    CHECK(fs_create_file("new.txt") == 0);
    CHECK(file_is("new.txt", ""));
    CHECK(fs_create_folder("newdir") == 0);

    CHECK(fs_write_file("new.txt", "keep", 4) == 0);
    CHECK(fs_create_file("new.txt") == -1);
    CHECK(errno == EEXIST);
    CHECK(file_is("new.txt", "keep"));
    CHECK(fs_create_folder("newdir") == -1);
    CHECK(errno == EEXIST);
}

static void test_read_and_write(void) {
    CHECK(fs_write_file("data.txt", "one\ntwo\n", 8) == 0);
    CHECK(file_is("data.txt", "one\ntwo\n"));
    CHECK(fs_write_file("data.txt", "x", 1) == 0);
    CHECK(file_is("data.txt", "x"));
    CHECK(fs_write_file("data.txt", "", 0) == 0);
    CHECK(file_is("data.txt", ""));

    char *data;
    size_t len;
    CHECK(fs_read_file("missing.txt", &data, &len) == -1);
    CHECK(errno == ENOENT);
    CHECK(fs_create_folder("a_folder") == 0);
    CHECK(fs_read_file("a_folder", &data, &len) == -1);
    CHECK(errno == EISDIR);

    /* Must return an error, not hang on the open. */
    CHECK(mkfifo("a_pipe", 0644) == 0);
    CHECK(fs_read_file("a_pipe", &data, &len) == -1);
    CHECK(errno == ENOTSUP);
}

static void test_rename(void) {
    CHECK(fs_write_file("old.txt", "old", 3) == 0);
    CHECK(fs_rename("old.txt", "renamed.txt") == 0);
    CHECK(!exists("old.txt"));
    CHECK(file_is("renamed.txt", "old"));

    CHECK(fs_write_file("other.txt", "other", 5) == 0);
    CHECK(fs_rename("renamed.txt", "other.txt") == -1);
    CHECK(errno == EEXIST);
    CHECK(file_is("renamed.txt", "old"));
    CHECK(file_is("other.txt", "other"));

    CHECK(fs_create_folder("old_dir") == 0);
    CHECK(fs_rename("old_dir", "new_dir") == 0);
    CHECK(!exists("old_dir"));
    CHECK(exists("new_dir"));
}

static void test_delete(void) {
    CHECK(fs_create_file("gone.txt") == 0);
    CHECK(fs_delete("gone.txt") == 0);
    CHECK(!exists("gone.txt"));

    CHECK(fs_create_folder("tree") == 0);
    CHECK(fs_create_folder("tree/sub") == 0);
    CHECK(fs_create_folder("tree/sub/empty") == 0);
    CHECK(fs_create_file("tree/file") == 0);
    CHECK(fs_create_file("tree/sub/file") == 0);
    CHECK(fs_delete("tree") == 0);
    CHECK(!exists("tree"));

    CHECK(fs_delete("missing") == -1);
    CHECK(errno == ENOENT);
}

static void test_delete_does_not_follow_links(void) {
    CHECK(fs_create_folder("precious") == 0);
    CHECK(fs_create_file("precious/file") == 0);

    CHECK(symlink("precious", "link") == 0);
    CHECK(fs_delete("link") == 0);
    CHECK(!exists("link"));
    CHECK(exists("precious/file"));

    CHECK(fs_create_folder("holder") == 0);
    CHECK(symlink("../precious", "holder/link") == 0);
    CHECK(fs_delete("holder") == 0);
    CHECK(!exists("holder"));
    CHECK(exists("precious/file"));
}

static void test_list(void) {
    CHECK(fs_create_folder("listing") == 0);
    CHECK(fs_create_file("listing/b.txt") == 0);
    CHECK(fs_create_file("listing/a.txt") == 0);
    CHECK(fs_create_folder("listing/z_dir") == 0);
    CHECK(symlink("z_dir", "listing/link_to_dir") == 0);
    CHECK(symlink("nowhere", "listing/broken_link") == 0);

    Entry *entries;
    int count;
    CHECK(fs_list("listing", &entries, &count) == 0);
    CHECK(count == 5);
    if (count == 5) {
        const char *names[] = {"link_to_dir", "z_dir", "a.txt", "b.txt", "broken_link"};
        const int dirs[] = {1, 1, 0, 0, 0};
        for (int i = 0; i < 5; i++) {
            CHECK(strcmp(entries[i].name, names[i]) == 0);
            CHECK(entries[i].is_dir == dirs[i]);
        }
    }
    fs_free_list(entries, count);

    CHECK(fs_list("missing", &entries, &count) == -1);
    CHECK(errno == ENOENT);

    CHECK(fs_create_folder("empty_listing") == 0);
    CHECK(fs_list("empty_listing", &entries, &count) == 0);
    CHECK(count == 0);
    fs_free_list(entries, count);
}

static void test_names_and_paths(void) {
    CHECK(fs_valid_name("file.txt"));
    CHECK(fs_valid_name(".hidden"));
    CHECK(!fs_valid_name(""));
    CHECK(!fs_valid_name("."));
    CHECK(!fs_valid_name(".."));
    CHECK(!fs_valid_name("a/b"));
    CHECK(!fs_valid_name("../escape"));

    struct { const char *path, *parent; } parents[] = {
        {"a", "."}, {"a/b", "a"}, {"a/b/c", "a/b"}, {"/x", "/"}, {"/x/y", "/x"},
    };
    for (size_t i = 0; i < sizeof(parents) / sizeof(parents[0]); i++) {
        char *parent = fs_parent(parents[i].path);
        CHECK(strcmp(parent, parents[i].parent) == 0);
        free(parent);
    }

    char *joined = fs_join(".", "a");
    CHECK(strcmp(joined, "a") == 0);
    free(joined);
    joined = fs_join("a/b", "c");
    CHECK(strcmp(joined, "a/b/c") == 0);
    free(joined);
}

static void test_tree(void) {
    CHECK(fs_create_folder("t") == 0);
    CHECK(chdir("t") == 0);
    CHECK(fs_create_folder("src") == 0);
    CHECK(fs_create_folder("src/deep") == 0);
    CHECK(fs_create_file("src/deep/d.c") == 0);
    CHECK(fs_create_file("src/a.c") == 0);
    CHECK(fs_create_file("readme") == 0);

    Tree tree = {0};
    CHECK(tree_refresh(&tree) == 0);
    CHECK(tree.count == 2);
    CHECK(strcmp(tree.rows[0].path, "src") == 0);
    CHECK(tree.rows[0].is_dir == 1);
    CHECK(strcmp(tree.rows[1].path, "readme") == 0);

    tree_set_expanded(&tree, "src", 1);
    CHECK(tree.count == 4);
    CHECK(tree_find(&tree, "src/deep") == 1);
    CHECK(tree_find(&tree, "src/a.c") == 2);
    CHECK(tree.rows[1].depth == 1);
    CHECK(strcmp(tree.rows[2].name, "a.c") == 0);

    tree_set_expanded(&tree, "src/deep", 1);
    CHECK(tree.count == 5);
    CHECK(tree_find(&tree, "src/deep/d.c") == 2);
    CHECK(tree.rows[2].depth == 2);

    /* Refreshing picks up new files and keeps folders open. */
    CHECK(fs_create_file("src/deep/new.c") == 0);
    CHECK(tree_refresh(&tree) == 0);
    CHECK(tree.count == 6);
    CHECK(tree_find(&tree, "src/deep/new.c") >= 0);

    CHECK(fs_delete("src/deep") == 0);
    CHECK(tree_refresh(&tree) == 0);
    CHECK(tree.count == 3);

    tree_set_expanded(&tree, "src", 0);
    CHECK(tree.count == 2);
    CHECK(tree_find(&tree, "src/a.c") == -1);

    /* Files cannot be expanded and unknown paths are ignored. */
    tree_set_expanded(&tree, "readme", 1);
    CHECK(tree.rows[1].expanded == 0);
    tree_set_expanded(&tree, "missing", 1);
    CHECK(tree.count == 2);

    tree_free(&tree);
    CHECK(chdir("..") == 0);
}

static void test_editor_editing(void) {
    CHECK(fs_write_file("e.txt", "ab\ncd", 5) == 0);
    Editor e;
    CHECK(editor_open(&e, "e.txt") == 0);
    CHECK(e.row == 0 && e.col == 0 && e.buffer.dirty == 0);

    editor_insert_char(&e, 'X');
    CHECK(e.col == 1);
    CHECK(e.buffer.dirty == 1);
    editor_insert_newline(&e);
    CHECK(e.row == 1 && e.col == 0);
    CHECK(buffer_is(&e.buffer, "X\nab\ncd"));

    editor_backspace(&e); /* Joins with the line above. */
    CHECK(e.row == 0 && e.col == 1);
    CHECK(buffer_is(&e.buffer, "Xab\ncd"));
    editor_backspace(&e);
    CHECK(e.col == 0);
    editor_backspace(&e); /* At the very start: nothing to do. */
    CHECK(buffer_is(&e.buffer, "ab\ncd"));

    editor_move(&e, MOVE_LINE_END, 10, 0);
    editor_delete_forward(&e); /* Joins with the line below. */
    CHECK(buffer_is(&e.buffer, "abcd"));
    editor_move(&e, MOVE_LINE_END, 10, 0);
    editor_delete_forward(&e); /* At the very end: nothing to do. */
    CHECK(buffer_is(&e.buffer, "abcd"));

    CHECK(editor_save(&e) == 0);
    CHECK(e.buffer.dirty == 0);
    CHECK(file_is("e.txt", "abcd"));

    editor_set_path(&e, "e2.txt");
    CHECK(editor_save(&e) == 0);
    CHECK(file_is("e2.txt", "abcd"));
    editor_close(&e);

    CHECK(editor_open(&e, "missing.txt") == -1);
    CHECK(errno == ENOENT);
}

static void test_editor_cursor(void) {
    CHECK(fs_write_file("c.txt", "long line\nx\n\tab", 15) == 0);
    Editor e;
    CHECK(editor_open(&e, "c.txt") == 0);

    editor_move(&e, MOVE_LEFT, 10, 0); /* At the start of the file: stays. */
    CHECK(e.row == 0 && e.col == 0);

    editor_move(&e, MOVE_LINE_END, 10, 0);
    CHECK(e.col == 9);
    editor_move(&e, MOVE_DOWN, 10, 0); /* Shorter line: the column is clamped. */
    CHECK(e.row == 1 && e.col == 1);
    editor_move(&e, MOVE_RIGHT, 10, 0); /* Wraps to the next line. */
    CHECK(e.row == 2 && e.col == 0);
    editor_move(&e, MOVE_LEFT, 10, 0); /* Wraps back. */
    CHECK(e.row == 1 && e.col == 1);

    editor_move(&e, MOVE_PAGE_DOWN, 100, 0);
    CHECK(e.row == 2);
    editor_move(&e, MOVE_PAGE_UP, 100, 0);
    CHECK(e.row == 0);

    /* Tabs take up to TAB_WIDTH screen columns. */
    CHECK(editor_screen_col(&e, 2, 1) == TAB_WIDTH);
    CHECK(editor_screen_col(&e, 2, 2) == TAB_WIDTH + 1);

    editor_click(&e, 2, 0, 0);
    CHECK(e.row == 2 && e.col == 0);
    editor_click(&e, 2, TAB_WIDTH - 1, 0); /* Inside the tab. */
    CHECK(e.col == 0);
    editor_click(&e, 2, TAB_WIDTH, 0);
    CHECK(e.col == 1);
    editor_click(&e, 2, 100, 0); /* Past the end of the line. */
    CHECK(e.col == 3);
    editor_click(&e, 100, 0, 0); /* Past the end of the file. */
    CHECK(e.row == 2 && e.col == 0);
    editor_click(&e, -5, 0, 0);
    CHECK(e.row == 0);
    editor_close(&e);
}

static int selected_is(const Editor *e, const char *expected) {
    char *text = editor_selected_text(e);
    int same = expected == NULL ? text == NULL : (text != NULL && strcmp(text, expected) == 0);
    free(text);
    return same;
}

static void open_with(Editor *e, const char *text) {
    CHECK(fs_write_file("sel.txt", text, strlen(text)) == 0);
    CHECK(editor_open(e, "sel.txt") == 0);
}

static void test_editor_selection(void) {
    Editor e;
    open_with(&e, "hello\nworld\nthird");
    CHECK(selected_is(&e, NULL));

    /* Forwards and backwards from the anchor give the same ordered text. */
    editor_move(&e, MOVE_RIGHT, 10, 0);
    editor_move(&e, MOVE_RIGHT, 10, 1);
    editor_move(&e, MOVE_RIGHT, 10, 1);
    CHECK(selected_is(&e, "el"));
    editor_move(&e, MOVE_LEFT, 10, 1);
    editor_move(&e, MOVE_LEFT, 10, 1);
    CHECK(selected_is(&e, NULL)); /* Back at the anchor: nothing is selected. */
    editor_move(&e, MOVE_LEFT, 10, 1);
    CHECK(selected_is(&e, "h"));

    /* Across lines. */
    editor_move(&e, MOVE_RIGHT, 10, 0); /* Collapses to the end of the selection. */
    CHECK(e.row == 0 && e.col == 1);
    CHECK(selected_is(&e, NULL));
    editor_move(&e, MOVE_DOWN, 10, 1);
    CHECK(selected_is(&e, "ello\nw"));

    /* Left collapses to the start, and other moves just clear it. */
    editor_move(&e, MOVE_LEFT, 10, 0);
    CHECK(e.row == 0 && e.col == 1);
    editor_move(&e, MOVE_DOWN, 10, 1);
    editor_move(&e, MOVE_DOWN, 10, 0);
    CHECK(e.row == 2 && e.col == 1);
    CHECK(selected_is(&e, NULL));

    /* Mouse-style selection. */
    editor_click(&e, 0, 0, 0);
    editor_click(&e, 1, 3, 1);
    CHECK(selected_is(&e, "hello\nwor"));
    editor_click(&e, 0, 2, 1); /* Dragging back past the anchor. */
    CHECK(selected_is(&e, "he"));
    editor_click(&e, 2, 0, 0);
    CHECK(selected_is(&e, NULL));
    editor_close(&e);

    /* Editing replaces the selection. */
    open_with(&e, "hello\nworld");
    editor_click(&e, 0, 1, 0);
    editor_click(&e, 1, 2, 1);
    editor_insert_char(&e, 'X');
    CHECK(buffer_is(&e.buffer, "hXrld"));
    CHECK(e.row == 0 && e.col == 2);
    CHECK(selected_is(&e, NULL));

    editor_click(&e, 0, 1, 0);
    editor_click(&e, 0, 3, 1);
    editor_backspace(&e);
    CHECK(buffer_is(&e.buffer, "hld"));
    CHECK(e.col == 1);

    editor_click(&e, 0, 0, 0);
    editor_click(&e, 0, 2, 1);
    editor_delete_forward(&e);
    CHECK(buffer_is(&e.buffer, "d"));

    editor_click(&e, 0, 0, 0);
    editor_click(&e, 0, 1, 1);
    editor_insert_newline(&e);
    CHECK(buffer_is(&e.buffer, "\n"));
    CHECK(e.row == 1 && e.col == 0);
    editor_close(&e);
}

static void test_editor_paste(void) {
    Editor e;
    open_with(&e, "ab");
    editor_click(&e, 0, 1, 0);
    editor_insert_text(&e, "1\n2\n3", 5);
    CHECK(buffer_is(&e.buffer, "a1\n2\n3b"));
    CHECK(e.row == 2 && e.col == 1);

    editor_click(&e, 0, 0, 0);
    editor_click(&e, 2, 1, 1);
    editor_insert_text(&e, "Z", 1); /* Replaces the selection. */
    CHECK(buffer_is(&e.buffer, "Zb"));

    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "a1\n2\n3b"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "ab"));
    CHECK(e.row == 0 && e.col == 1);

    size_t before = (size_t)e.undo_count;
    editor_insert_text(&e, "", 0); /* Empty clipboard: not an edit. */
    CHECK((size_t)e.undo_count == before);
    CHECK(e.buffer.dirty == 0);
    editor_close(&e);
}

static void type_text(Editor *e, const char *text) {
    for (const char *p = text; *p != '\0'; p++) editor_insert_char(e, *p);
}

static void test_editor_undo(void) {
    Editor e;
    open_with(&e, "start");
    CHECK(editor_undo(&e) == 0); /* Nothing to undo yet. */

    /* A word typed in one go is one step, and a space begins the next. */
    type_text(&e, "one two");
    CHECK(buffer_is(&e.buffer, "one twostart"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "onestart"));
    CHECK(e.col == 3);
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "start"));
    CHECK(e.col == 0);
    CHECK(editor_undo(&e) == 0);

    /* Moving the cursor ends a step, even when typing resumes in the same place. */
    type_text(&e, "ab");
    editor_move(&e, MOVE_LEFT, 10, 0);
    editor_move(&e, MOVE_RIGHT, 10, 0);
    type_text(&e, "cd");
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "abstart"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "start"));

    /* Runs of Backspace and of Delete are one step each. */
    editor_click(&e, 0, 4, 0);
    editor_backspace(&e);
    editor_backspace(&e);
    CHECK(buffer_is(&e.buffer, "stt"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "start"));
    CHECK(e.col == 4);

    editor_click(&e, 0, 1, 0);
    editor_delete_forward(&e);
    editor_delete_forward(&e);
    CHECK(buffer_is(&e.buffer, "srt"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "start"));
    CHECK(e.col == 1);

    /* Different kinds of edit are separate steps. */
    type_text(&e, "x");
    editor_backspace(&e);
    editor_delete_forward(&e);
    CHECK(buffer_is(&e.buffer, "sart"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "start"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "sxtart"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "start"));
    editor_close(&e);

    /* Line breaks, and Backspace and Delete across them. */
    open_with(&e, "ab\ncd");
    editor_click(&e, 0, 1, 0);
    editor_insert_newline(&e);
    CHECK(buffer_is(&e.buffer, "a\nb\ncd"));
    type_text(&e, "x");
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "a\nb\ncd"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "ab\ncd"));
    CHECK(e.row == 0 && e.col == 1);

    editor_click(&e, 1, 0, 0);
    editor_backspace(&e);
    editor_backspace(&e);
    CHECK(buffer_is(&e.buffer, "acd"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "ab\ncd"));
    CHECK(e.row == 1 && e.col == 0);

    editor_click(&e, 0, 2, 0);
    editor_delete_forward(&e);
    editor_delete_forward(&e);
    CHECK(buffer_is(&e.buffer, "abd"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "ab\ncd"));
    CHECK(e.row == 0 && e.col == 2);

    /* Text replaced by typing comes back, along with what was typed after it. */
    editor_click(&e, 0, 0, 0);
    editor_click(&e, 1, 1, 1);
    type_text(&e, "xyz");
    CHECK(buffer_is(&e.buffer, "xyzd"));
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "ab\ncd"));
    CHECK(selected_is(&e, NULL));

    /* Edits that change nothing leave no step. */
    int steps = e.undo_count;
    editor_click(&e, 0, 0, 0);
    editor_backspace(&e);
    editor_click(&e, 1, 100, 0);
    editor_delete_forward(&e);
    CHECK(e.undo_count == steps);
    editor_close(&e);
}

static void test_editor_undo_and_dirty(void) {
    Editor e;
    open_with(&e, "x");

    /* Undoing back to the saved text means there is nothing left to save. */
    type_text(&e, "a");
    CHECK(e.buffer.dirty == 1);
    CHECK(editor_undo(&e) == 1);
    CHECK(e.buffer.dirty == 0);

    /* The same holds for a later save. */
    type_text(&e, "a");
    CHECK(editor_save(&e) == 0);
    CHECK(e.buffer.dirty == 0);
    type_text(&e, "b"); /* Must not merge with the step already saved. */
    CHECK(e.buffer.dirty == 1);
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "ax"));
    CHECK(e.buffer.dirty == 0);

    /* Undoing past the save differs from the file again. */
    CHECK(editor_undo(&e) == 1);
    CHECK(buffer_is(&e.buffer, "x"));
    CHECK(e.buffer.dirty == 1);
    CHECK(file_is("sel.txt", "ax"));

    /* A different edit at the same depth must not look saved. */
    type_text(&e, "z");
    CHECK(buffer_is(&e.buffer, "zx"));
    CHECK(e.buffer.dirty == 1);
    CHECK(editor_undo(&e) == 1);
    CHECK(e.buffer.dirty == 1);
    editor_close(&e);
}

static void test_editor_undo_limit(void) {
    Editor e;
    open_with(&e, "");

    /* Each newline is its own step. */
    for (int i = 0; i < 1100; i++) editor_insert_newline(&e);
    CHECK(e.undo_count == 1000);

    int undone = 0;
    while (editor_undo(&e)) undone++;
    CHECK(undone == 1000);
    CHECK(e.buffer.count == 101);

    /* The original text is out of reach, so the buffer stays dirty. */
    CHECK(e.buffer.dirty == 1);
    editor_close(&e);
}

static void test_editor_scrolling(void) {
    char text[400] = "";
    for (int i = 0; i < 20; i++) strcat(text, "0123456789012345\n");
    CHECK(fs_write_file("s.txt", text, strlen(text)) == 0);

    Editor e;
    CHECK(editor_open(&e, "s.txt") == 0);
    editor_click(&e, 15, 0, 0);
    editor_keep_cursor_visible(&e, 5, 10);
    CHECK(e.top == 11);
    editor_click(&e, 2, 0, 0);
    editor_keep_cursor_visible(&e, 5, 10);
    CHECK(e.top == 2);
    editor_click(&e, 2, 12, 0);
    editor_keep_cursor_visible(&e, 5, 10);
    CHECK(e.left == 3);
    editor_click(&e, 2, 0, 0);
    editor_keep_cursor_visible(&e, 5, 10);
    CHECK(e.left == 0);
    editor_close(&e);
}

int main(void) {
    char folder[1024];
    enter_temp_folder(folder, sizeof(folder));

    test_buffer_round_trip();
    test_buffer_splits_lines();
    test_buffer_keeps_nul_bytes();
    test_buffer_insert();
    test_buffer_ranges();
    test_buffer_delete_then_insert_restores();
    test_create();
    test_read_and_write();
    test_rename();
    test_delete();
    test_delete_does_not_follow_links();
    test_list();
    test_names_and_paths();
    test_tree();
    test_editor_editing();
    test_editor_cursor();
    test_editor_selection();
    test_editor_paste();
    test_editor_undo();
    test_editor_undo_and_dirty();
    test_editor_undo_limit();
    test_editor_scrolling();

    return finish(folder);
}
