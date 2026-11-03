#include "fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

/* Buffer lengths are ints, so refuse anything that could overflow them. */
#define MAX_FILE_SIZE (64 * 1024 * 1024)

int fs_create_file(const char *path) {
    FILE *f = fopen(path, "wx");
    if (f == NULL) return -1;
    return fclose(f) == 0 ? 0 : -1;
}

int fs_create_folder(const char *path) {
    return mkdir(path, 0755);
}

int fs_read_file(const char *path, char **out, size_t *out_len) {
    /* Check the type before opening: opening a named pipe blocks forever. */
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        errno = EISDIR;
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        errno = ENOTSUP;
        return -1;
    }
    if (st.st_size > MAX_FILE_SIZE) {
        errno = EFBIG;
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;

    size_t size = (size_t)st.st_size;
    char *data = xrealloc(NULL, size + 1);
    size_t got = fread(data, 1, size, f);
    int read_failed = ferror(f);
    fclose(f);
    if (read_failed) {
        free(data);
        errno = EIO;
        return -1;
    }

    data[got] = '\0';
    *out = data;
    *out_len = got;
    return 0;
}

int fs_write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -1;

    size_t written = fwrite(data, 1, len, f);
    int saved_errno = errno;
    /* fclose flushes, so a full disk may only be reported here. */
    if (fclose(f) != 0) return -1;
    if (written != len) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int fs_rename(const char *old_path, const char *new_path) {
    /* rename() silently overwrites an existing file. */
    struct stat st;
    if (lstat(new_path, &st) == 0) {
        errno = EEXIST;
        return -1;
    }
    return rename(old_path, new_path);
}

char *fs_join(const char *folder, const char *name) {
    if (strcmp(folder, ".") == 0) return xstrdup(name);
    size_t size = strlen(folder) + strlen(name) + 2;
    char *path = xrealloc(NULL, size);
    snprintf(path, size, "%s/%s", folder, name);
    return path;
}

char *fs_parent(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL) return xstrdup(".");
    if (slash == path) return xstrdup("/");

    size_t len = (size_t)(slash - path);
    char *parent = xrealloc(NULL, len + 1);
    memcpy(parent, path, len);
    parent[len] = '\0';
    return parent;
}

static int is_dot_or_dot_dot(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

int fs_delete(const char *path) {
    /* lstat, not stat: following a link to a folder would empty that folder. */
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);

    DIR *dir = opendir(path);
    if (dir == NULL) return -1;

    int result = 0;
    struct dirent *item;
    while (result == 0 && (item = readdir(dir)) != NULL) {
        if (is_dot_or_dot_dot(item->d_name)) continue;
        char *child = fs_join(path, item->d_name);
        result = fs_delete(child);
        int saved_errno = errno;
        free(child);
        errno = saved_errno;
    }

    int saved_errno = errno;
    closedir(dir);
    if (result != 0) {
        errno = saved_errno;
        return -1;
    }
    return rmdir(path);
}

static int compare_entries(const void *a, const void *b) {
    const Entry *ea = a;
    const Entry *eb = b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return strcmp(ea->name, eb->name);
}

void fs_free_list(Entry *entries, int count) {
    for (int i = 0; i < count; i++) {
        free(entries[i].name);
    }
    free(entries);
}

int fs_list(const char *path, Entry **out, int *out_count) {
    DIR *dir = opendir(path);
    if (dir == NULL) return -1;

    Entry *entries = NULL;
    int count = 0;
    int capacity = 0;
    struct dirent *item;

    while ((item = readdir(dir)) != NULL) {
        if (is_dot_or_dot_dot(item->d_name)) continue;

        if (count == capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            entries = xrealloc(entries, sizeof(Entry) * (size_t)capacity);
        }

        /* stat follows links, so a link to a folder opens as a folder. A broken
         * link fails here and is listed as a file. */
        char *full_path = fs_join(path, item->d_name);
        struct stat st;
        entries[count].name = xstrdup(item->d_name);
        entries[count].is_dir = stat(full_path, &st) == 0 && S_ISDIR(st.st_mode);
        count++;
        free(full_path);
    }

    closedir(dir);
    if (count > 0) qsort(entries, (size_t)count, sizeof(Entry), compare_entries);
    *out = entries;
    *out_count = count;
    return 0;
}

int fs_valid_name(const char *name) {
    return name[0] != '\0' && !is_dot_or_dot_dot(name) && strchr(name, '/') == NULL;
}
