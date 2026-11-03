#include "fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Lengths in the buffer are ints, so we refuse anything that could overflow. */
#define MAX_FILE_SIZE (64 * 1024 * 1024)

int fs_create_file(const char *path) {
    /* The "x" makes fopen fail if the file already exists. */
    FILE *f = fopen(path, "wx");
    if (f == NULL) return -1;
    return fclose(f) == 0 ? 0 : -1;
}

int fs_create_folder(const char *path) {
    return mkdir(path, 0755); /* Owner can write; everyone can read and enter. */
}

int fs_read_file(const char *path, char **out, size_t *out_len) {
    /* Check the type first: opening a named pipe would block forever. */
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
    char *data = malloc(size + 1);
    if (data == NULL) {
        fclose(f);
        errno = ENOMEM;
        return -1;
    }

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
    /* Check fclose too: it writes out whatever stdio was still holding, so a
     * full disk may only show up here. */
    if (fclose(f) != 0) return -1;
    if (written != len) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int fs_rename(const char *old_path, const char *new_path) {
    /* rename() would silently overwrite an existing file, so look first. */
    struct stat st;
    if (lstat(new_path, &st) == 0) {
        errno = EEXIST;
        return -1;
    }
    return rename(old_path, new_path);
}

/* Build "folder/name" in new memory, or return NULL if out of memory. */
static char *join_path(const char *folder, const char *name) {
    size_t size = strlen(folder) + strlen(name) + 2; /* '/' and '\0'. */
    char *path = malloc(size);
    if (path != NULL) snprintf(path, size, "%s/%s", folder, name);
    return path;
}

static int is_dot_or_dot_dot(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

int fs_delete(const char *path) {
    /* lstat describes a symbolic link itself, while stat would follow it. With
     * stat, deleting a link to a folder would empty the folder it points to. */
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);

    /* rmdir only works on empty folders, so delete the contents first. A
     * function calling itself like this is called recursion. */
    DIR *dir = opendir(path);
    if (dir == NULL) return -1;

    int result = 0;
    struct dirent *item;
    while (result == 0 && (item = readdir(dir)) != NULL) {
        if (is_dot_or_dot_dot(item->d_name)) continue;

        char *child = join_path(path, item->d_name);
        if (child == NULL) {
            errno = ENOMEM;
            result = -1;
        } else {
            result = fs_delete(child);
            free(child);
        }
    }

    int saved_errno = errno;
    closedir(dir);
    if (result != 0) {
        errno = saved_errno;
        return -1;
    }
    return rmdir(path);
}

/* qsort calls this to ask which of two entries should come first. It returns a
 * negative number if a goes first, and a positive number if b does. */
static int compare_entries(const void *a, const void *b) {
    const Entry *ea = a;
    const Entry *eb = b;
    if (strcmp(ea->name, "..") == 0) return -1;
    if (strcmp(eb->name, "..") == 0) return 1;
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
        if (strcmp(item->d_name, ".") == 0) continue;

        /* Grow the array by doubling it, so we rarely have to reallocate. */
        if (count == capacity) {
            int new_capacity = capacity == 0 ? 16 : capacity * 2;
            Entry *grown = realloc(entries, sizeof(Entry) * (size_t)new_capacity);
            if (grown == NULL) goto out_of_memory;
            entries = grown;
            capacity = new_capacity;
        }

        char *name = strdup(item->d_name);
        char *full_path = join_path(path, item->d_name);
        if (name == NULL || full_path == NULL) {
            free(name);
            free(full_path);
            goto out_of_memory;
        }

        /* stat follows links, so a link to a folder can be opened as a folder.
         * A broken link fails here and is simply shown as a file. */
        struct stat st;
        entries[count].name = name;
        entries[count].is_dir = stat(full_path, &st) == 0 && S_ISDIR(st.st_mode);
        count++;
        free(full_path);
    }

    closedir(dir);
    qsort(entries, (size_t)count, sizeof(Entry), compare_entries);
    *out = entries;
    *out_count = count;
    return 0;

out_of_memory:
    /* goto is a common C pattern for sharing one cleanup path. */
    closedir(dir);
    fs_free_list(entries, count);
    errno = ENOMEM;
    return -1;
}

int fs_valid_name(const char *name) {
    return name[0] != '\0' && !is_dot_or_dot_dot(name) && strchr(name, '/') == NULL;
}
