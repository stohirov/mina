#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"

static int failures = 0;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

/* Does the file hold exactly these bytes? */
static int file_is(const char *path, const char *expected) {
    char *data = NULL;
    size_t len = 0;
    if (fs_read_file(path, &data, &len) != 0) return 0;
    int same = len == strlen(expected) && memcmp(data, expected, len) == 0;
    free(data);
    return same;
}

/* Tests run in a new temporary folder, so they cannot touch real files. */
static void enter_temp_folder(char *folder, size_t size) {
    const char *base = getenv("TMPDIR");
    snprintf(folder, size, "%s/editor_test_XXXXXX", base != NULL ? base : "/tmp");
    if (mkdtemp(folder) == NULL || chdir(folder) != 0) {
        perror("could not set up the test folder");
        exit(1);
    }
}

static int finish(const char *folder) {
    if (chdir("/") != 0 || fs_delete(folder) != 0) perror("could not clean up");
    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}

#endif
