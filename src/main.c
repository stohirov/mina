/* Usage:
 *   ./editor            open the current folder
 *   ./editor folder     open that folder
 *   ./editor file.txt   open that file, with its folder in the tree
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <errno.h>
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

    /* Ctrl-click is the usual right-click on a Mac with one mouse button. */
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");

    if (!SDL_Init(SDL_INIT_VIDEO)) return fail("Cannot start SDL");

    SDL_Window *window;
    SDL_Renderer *renderer;
    if (!SDL_CreateWindowAndRenderer("Simple Editor", 1000, 700,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &window, &renderer)) {
        return fail("Cannot create window");
    }
    SDL_SetWindowMinimumSize(window, 500, 300);

    /* On a Retina display the window has more pixels than points. */
    App app;
    int font_size = (int)(FONT_POINTS * SDL_GetWindowPixelDensity(window));
    if (app_init(&app, renderer, font_size) != 0) return fail("Cannot load font");
    if (file != NULL) app_open_file(&app, file);

    SDL_StartTextInput(window);

    while (app.running) {
        SDL_Event event;
        /* The timeout lets SDL notice Ctrl-C from the terminal while idle. */
        if (!SDL_WaitEventTimeout(&event, 250)) continue;
        do {
            SDL_ConvertEventToRenderCoordinates(renderer, &event);
            app_handle_event(&app, &event);
        } while (SDL_PollEvent(&event));

        app_draw(&app);
        SDL_RenderPresent(renderer);
    }

    app_free(&app);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
