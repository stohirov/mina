#include "tree.h"

#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "util.h"

static int was_expanded(const Tree *old, const char *path) {
    int index = tree_find(old, path);
    return index >= 0 && old->rows[index].expanded;
}

static int add_rows(Tree *tree, const Tree *old, const char *folder, int depth) {
    Entry *entries;
    int count;
    if (fs_list(folder, &entries, &count) != 0) return -1;

    for (int i = 0; i < count; i++) {
        tree->rows = xrealloc(tree->rows, sizeof(TreeRow) * ((size_t)tree->count + 1));
        TreeRow *row = &tree->rows[tree->count++];
        row->path = fs_join(folder, entries[i].name);
        row->name = xstrdup(entries[i].name);
        row->depth = depth;
        row->is_dir = entries[i].is_dir;
        row->expanded = row->is_dir && was_expanded(old, row->path);

        if (row->expanded) {
            char *path = xstrdup(row->path); /* add_rows may move the rows. */
            /* An unreadable subfolder just shows no children. */
            add_rows(tree, old, path, depth + 1);
            free(path);
        }
    }

    fs_free_list(entries, count);
    return 0;
}

int tree_refresh(Tree *tree) {
    Tree next = {0};
    int result = add_rows(&next, tree, ".", 0);
    tree_free(tree);
    *tree = next;
    return result;
}

void tree_free(Tree *tree) {
    for (int i = 0; i < tree->count; i++) {
        free(tree->rows[i].path);
        free(tree->rows[i].name);
    }
    free(tree->rows);
    tree->rows = NULL;
    tree->count = 0;
}

int tree_find(const Tree *tree, const char *path) {
    for (int i = 0; i < tree->count; i++) {
        if (strcmp(tree->rows[i].path, path) == 0) return i;
    }
    return -1;
}

void tree_set_expanded(Tree *tree, const char *path, int expanded) {
    int index = tree_find(tree, path);
    if (index < 0 || !tree->rows[index].is_dir) return;
    tree->rows[index].expanded = expanded;
    tree_refresh(tree);
}
