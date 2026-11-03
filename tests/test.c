/* test.c - tests for the text buffer and the file operations. Run: make test
 *
 * No framework: CHECK prints a line for each failure and main() counts them. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "fs.h"

static int failures = 0;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static int exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

/* Does the file hold exactly this text? */
static int file_is(const char *path, const char *expected) {
    char *data = NULL;
    size_t len = 0;
    if (fs_read_file(path, &data, &len) != 0) return 0;
    int same = len == strlen(expected) && memcmp(data, expected, len) == 0;
    free(data);
    return same;
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

static void test_buffer_insert_and_delete(void) {
    Buffer b;
    buffer_init(&b, "ac", 2);

    buffer_insert_char(&b, 0, 1, 'b'); /* Middle. */
    CHECK(buffer_is(&b, "abc"));
    CHECK(b.dirty == 1);
    buffer_insert_char(&b, 0, 0, '<'); /* Start. */
    buffer_insert_char(&b, 0, 4, '>'); /* End. */
    CHECK(buffer_is(&b, "<abc>"));
    CHECK(b.lines[0].len == 5);

    buffer_delete_char(&b, 0, 4);
    buffer_delete_char(&b, 0, 0);
    buffer_delete_char(&b, 0, 1);
    CHECK(buffer_is(&b, "ac"));
    CHECK(b.lines[0].len == 2);
    buffer_free(&b);
}

static void test_buffer_split_and_join(void) {
    Buffer b;
    buffer_init(&b, "hello\nworld", 11);

    buffer_split_line(&b, 0, 2);
    CHECK(b.count == 3);
    CHECK(buffer_is(&b, "he\nllo\nworld"));
    CHECK(b.dirty == 1);

    buffer_split_line(&b, 2, 5); /* At the very end of the last line. */
    CHECK(b.count == 4);
    CHECK(buffer_is(&b, "he\nllo\nworld\n"));

    buffer_join_with_next(&b, 2);
    CHECK(buffer_is(&b, "he\nllo\nworld"));
    buffer_join_with_next(&b, 0);
    CHECK(b.count == 2);
    CHECK(buffer_is(&b, "hello\nworld"));
    CHECK(b.lines[0].len == 5);
    buffer_free(&b);
}

static void test_create(void) {
    CHECK(fs_create_file("new.txt") == 0);
    CHECK(file_is("new.txt", ""));
    CHECK(fs_create_folder("newdir") == 0);

    /* Creating must never destroy what is already there. */
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
    CHECK(fs_write_file("data.txt", "x", 1) == 0); /* Shorter: must truncate. */
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

    /* A named pipe would hang forever if we tried to open it. */
    CHECK(mkfifo("a_pipe", 0644) == 0);
    CHECK(fs_read_file("a_pipe", &data, &len) == -1);
    CHECK(errno == ENOTSUP);
}

static void test_rename(void) {
    CHECK(fs_write_file("old.txt", "old", 3) == 0);
    CHECK(fs_rename("old.txt", "renamed.txt") == 0);
    CHECK(!exists("old.txt"));
    CHECK(file_is("renamed.txt", "old"));

    /* Must refuse to overwrite. */
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

    /* Deleting a link removes the link and not its target. */
    CHECK(symlink("precious", "link") == 0);
    CHECK(fs_delete("link") == 0);
    CHECK(!exists("link"));
    CHECK(exists("precious/file"));

    /* The same must hold for a link found inside a deleted folder. */
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
    CHECK(fs_create_folder("listing/.!odd_dir") == 0); /* Sorts before ".." by byte. */
    CHECK(symlink("z_dir", "listing/link_to_dir") == 0);
    CHECK(symlink("nowhere", "listing/broken_link") == 0);

    Entry *entries;
    int count;
    CHECK(fs_list("listing", &entries, &count) == 0);
    CHECK(count == 7);
    if (count == 7) {
        const char *names[] = {"..", ".!odd_dir", "link_to_dir", "z_dir", "a.txt", "b.txt", "broken_link"};
        const int dirs[] = {1, 1, 1, 1, 0, 0, 0};
        for (int i = 0; i < 7; i++) {
            CHECK(strcmp(entries[i].name, names[i]) == 0);
            CHECK(entries[i].is_dir == dirs[i]);
        }
    }
    fs_free_list(entries, count);

    CHECK(fs_list("missing", &entries, &count) == -1);
    CHECK(errno == ENOENT);
}

static void test_valid_name(void) {
    CHECK(fs_valid_name("file.txt"));
    CHECK(fs_valid_name(".hidden"));
    CHECK(!fs_valid_name(""));
    CHECK(!fs_valid_name("."));
    CHECK(!fs_valid_name(".."));
    CHECK(!fs_valid_name("a/b"));
    CHECK(!fs_valid_name("../escape"));
}

int main(void) {
    /* Work in a fresh temporary folder, so tests cannot touch real files. */
    const char *base = getenv("TMPDIR");
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s/editor_test_XXXXXX", base != NULL ? base : "/tmp");
    if (mkdtemp(folder) == NULL || chdir(folder) != 0) {
        perror("could not set up the test folder");
        return 1;
    }

    test_buffer_round_trip();
    test_buffer_splits_lines();
    test_buffer_keeps_nul_bytes();
    test_buffer_insert_and_delete();
    test_buffer_split_and_join();
    test_create();
    test_read_and_write();
    test_rename();
    test_delete();
    test_delete_does_not_follow_links();
    test_list();
    test_valid_name();

    if (chdir("/") != 0 || fs_delete(folder) != 0) perror("could not clean up");

    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}
