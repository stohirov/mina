/* editor.h - the screen where you edit a file. */
#ifndef EDITOR_H
#define EDITOR_H

/* Edit a file until the user closes it. Returns 0, or -1 with errno set if the
 * file could not be opened. */
int editor_run(const char *path);

#endif
