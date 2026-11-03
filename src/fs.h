/* fs.h - create, read, update and delete files and folders.
 *
 * Functions that return int give 0 on success and -1 on failure. On failure
 * errno says why, and strerror(errno) turns it into a readable message. */
#ifndef FS_H
#define FS_H

#include <stddef.h>

typedef struct {
    char *name;
    int is_dir;
} Entry;

/* Create. These fail with EEXIST rather than replace something. */
int fs_create_file(const char *path);
int fs_create_folder(const char *path);

/* Read a whole regular file into new memory, ending with '\0'. The caller must
 * free() it. Fails on anything else, such as a folder or a device. */
int fs_read_file(const char *path, char **out, size_t *out_len);

/* Update. fs_write_file replaces the contents of the file. fs_rename fails
 * with EEXIST rather than overwrite new_path. */
int fs_write_file(const char *path, const char *data, size_t len);
int fs_rename(const char *old_path, const char *new_path);

/* Delete a file, or a folder and everything inside it. A symbolic link is
 * removed itself; the thing it points to is left alone. */
int fs_delete(const char *path);

/* List a folder: ".." first, then folders, then files, each sorted by name.
 * Release the result with fs_free_list. */
int fs_list(const char *path, Entry **out, int *out_count);
void fs_free_list(Entry *entries, int count);

/* Returns 1 if name can be used for a new item in the current folder. */
int fs_valid_name(const char *name);

#endif
