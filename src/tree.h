/* The folder tree, stored as a flat list of the rows currently visible. Paths
 * are relative to the current working directory. */
#ifndef TREE_H
#define TREE_H

typedef struct {
    char *path;
    char *name;
    int depth;
    int is_dir;
    int expanded;
} TreeRow;

typedef struct {
    TreeRow *rows;
    int count;
} Tree;

/* Read the disk again, keeping open whichever folders are expanded. Returns -1
 * with errno set if the top folder cannot be listed. */
int tree_refresh(Tree *tree);
void tree_free(Tree *tree);

/* Returns the row index, or -1 if the path is not visible. */
int tree_find(const Tree *tree, const char *path);

/* Open or close a folder and refresh. Does nothing if the row is not visible. */
void tree_set_expanded(Tree *tree, const char *path, int expanded);

#endif
