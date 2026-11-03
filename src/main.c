/* main.c - where the program starts.
 *
 *   ./editor            browse the current folder
 *   ./editor folder     browse that folder
 *   ./editor file.txt   edit that file
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "browser.h"
#include "editor.h"
#include "terminal.h"

int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [file or folder]\n", argv[0]);
        return 1;
    }

    const char *file = NULL;
    if (argc == 2) {
        struct stat st;
        if (stat(argv[1], &st) != 0) {
            fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
            return 1;
        }
        if (!S_ISDIR(st.st_mode)) {
            file = argv[1];
        } else if (chdir(argv[1]) != 0) {
            fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
            return 1;
        }
    }

    if (terminal_enter() != 0) {
        fprintf(stderr, "This program must be run in a terminal.\n");
        return 1;
    }

    if (file == NULL) {
        browser_run();
    } else if (editor_run(file) != 0) {
        int saved_errno = errno;
        terminal_leave(); /* Otherwise the message would vanish with the screen. */
        fprintf(stderr, "%s: %s\n", file, strerror(saved_errno));
        return 1;
    }
    return 0;
}
