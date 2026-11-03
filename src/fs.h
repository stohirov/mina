/* File and folder operations. Functions returning int give 0 on success, or -1
 * with errno set on failure. */
#ifndef FS_H
#define FS_H

#include <stddef.h>

typedef struct {
    char *name;
    int is_dir;
} Entry;

/* Fail with EEXIST rather than replace something. */
int fs_create_file(const char *path);
int fs_create_folder(const char *path);

/* Read a regular file into new memory ending with '\0'. Free it with free(). */
int fs_read_file(const char *path, char **out, size_t *out_len);

int fs_write_file(const char *path, const char *data, size_t len);

/* Fails with EEXIST rather than overwrite new_path. */
int fs_rename(const char *old_path, const char *new_path);

/* Delete a file, or a folder with everything in it. A symbolic link is removed
 * itself; what it points to is left alone. */
int fs_delete(const char *path);

/* Folders first, then files, each sorted by name. Excludes "." and "..". */
int fs_list(const char *path, Entry **out, int *out_count);
void fs_free_list(Entry *entries, int count);

/* Returns 1 if name can be used for a new item inside a folder. */
int fs_valid_name(const char *name);

/* Path helpers. Both return new memory: free it with free().
 * fs_join(".", "a") is "a", and fs_parent("a") is ".". */
char *fs_join(const char *folder, const char *name);
char *fs_parent(const char *path);

#endif
