/* Usage:
 *   ./mina            open the current folder
 *   ./mina folder     open that folder
 *   ./mina file.txt   open that file, with its folder in the tree
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "fs.h"
#include "util.h"

#define FONT_POINTS 14

/* Make the tree's top folder the working directory. Returns the name of the
 * file to open, or NULL if there is none. */
static const char *enter_start_folder(const char *arg) {
    struct stat st;
    if (stat(arg, &st) != 0) {
        fprintf(stderr, "%s: %s\n", arg, strerror(errno));
        exit(1);
    }

    const char *file = NULL;
    char *folder;
    if (S_ISDIR(st.st_mode)) {
        folder = xstrdup(arg);
    } else {
        folder = fs_parent(arg);
        const char *slash = strrchr(arg, '/');
        file = slash != NULL ? slash + 1 : arg;
    }

    if (chdir(folder) != 0) {
        fprintf(stderr, "%s: %s\n", folder, strerror(errno));
        exit(1);
    }
    free(folder);
    return file;
}

/* An app opened from Finder or the Dock starts in "/", which is no place to edit
 * files. Start in the home folder instead. */
static void leave_root_folder(void) {
    char folder[2];
    const char *home = getenv("HOME");
    if (home != NULL && getcwd(folder, sizeof(folder)) != NULL && strcmp(folder, "/") == 0) {
        if (chdir(home) != 0) {
            fprintf(stderr, "%s: %s\n", home, strerror(errno));
        }
    }
}

static int font_size_for(SDL_Window *window) {
    return (int)(FONT_POINTS * SDL_GetWindowPixelDensity(window));
}

/* The picker may answer on another thread, so hand the result to the main loop
 * as an event. A list with no files in it means the user cancelled. */
static Uint32 folder_chosen_event;

static void SDLCALL on_folder_chosen(void *userdata, const char *const *files, int filter) {
    (void)userdata;
    (void)filter;
    if (files != NULL && files[0] == NULL) return;

    SDL_Event event;
    SDL_zero(event);
    event.type = folder_chosen_event;
    event.user.data1 = files != NULL ? SDL_strdup(files[0]) : NULL;
    event.user.data2 = files == NULL ? SDL_strdup(SDL_GetError()) : NULL;
    SDL_PushEvent(&event);
}

static void handle_folder_chosen(App *app, const SDL_UserEvent *event) {
    if (event->data1 != NULL) {
        app_open_folder(app, event->data1);
    } else {
        char message[512];
        SDL_snprintf(message, sizeof(message), "Cannot open the folder picker: %s", (char *)event->data2);
        app_show_message(app, message);
    }
    SDL_free(event->data1);
    SDL_free(event->data2);
}

/* The tree does not name its top folder, so the title bar does. */
static void update_window_title(SDL_Window *window) {
    static char shown[PATH_MAX];
    char folder[PATH_MAX];
    if (getcwd(folder, sizeof(folder)) == NULL || strcmp(folder, shown) == 0) return;
    SDL_strlcpy(shown, folder, sizeof(shown));

    const char *slash = strrchr(folder, '/');
    const char *name = slash != NULL && slash[1] != '\0' ? slash + 1 : folder;
    char title[PATH_MAX + 16];
    SDL_snprintf(title, sizeof(title), "%s - Mina", name);
    SDL_SetWindowTitle(window, title);
}

static int fail(const char *what) {
    fprintf(stderr, "%s: %s\n", what, SDL_GetError());
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [file or folder]\n", argv[0]);
        return 1;
    }
    const char *file = argc == 2 ? enter_start_folder(argv[1]) : NULL;
    if (argc == 1) leave_root_folder();

    /* Ctrl-click is the usual right-click on a Mac with one mouse button. */
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");

    if (!SDL_Init(SDL_INIT_VIDEO)) return fail("Cannot start SDL");

    SDL_Window *window;
    SDL_Renderer *renderer;
    if (!SDL_CreateWindowAndRenderer("Mina", 1000, 700,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &window, &renderer)) {
        return fail("Cannot create window");
    }
    /* Narrower than this, the tree and the buttons leave no room for tabs. */
    SDL_SetWindowMinimumSize(window, 700, 300);

    App app;
    if (app_init(&app, renderer, font_size_for(window)) != 0) return fail("Cannot load font");
    if (file != NULL) app_open_file(&app, file);

    SDL_StartTextInput(window);
    folder_chosen_event = SDL_RegisterEvents(1);

    while (app.running) {
        SDL_Event event;
        /* The timeout lets SDL notice Ctrl-C from the terminal while idle. */
        if (!SDL_WaitEventTimeout(&event, 250)) continue;
        do {
            SDL_ConvertEventToRenderCoordinates(renderer, &event);
            if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                app_set_font_size(&app, font_size_for(window));
            }
            if (event.type == folder_chosen_event) {
                handle_folder_chosen(&app, &event.user);
            } else {
                app_handle_event(&app, &event);
            }
        } while (SDL_PollEvent(&event));

        update_window_title(window);
        app_draw(&app);
        SDL_RenderPresent(renderer);

        if (app.wants_folder_dialog) {
            app.wants_folder_dialog = 0;
            char folder[PATH_MAX];
            const char *start = getcwd(folder, sizeof(folder));
            SDL_ShowOpenFolderDialog(on_folder_chosen, NULL, window, start, false);
        }
    }

    app_free(&app);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
