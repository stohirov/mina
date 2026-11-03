#include "browser.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "editor.h"
#include "fs.h"
#include "terminal.h"
#include "theme.h"
#include "ui.h"

/* "static" at file level means these are private to this file. */
static Entry *entries = NULL;
static int entry_count = 0;
static int selected = 0;
static int top = 0;

/* Read the current folder again. If select_name is given, highlight it. */
static void reload(const char *select_name) {
    fs_free_list(entries, entry_count);
    entries = NULL;
    entry_count = 0;

    if (fs_list(".", &entries, &entry_count) != 0) {
        ui_set_message("Cannot list folder: %s", strerror(errno));
    }

    if (select_name != NULL) {
        for (int i = 0; i < entry_count; i++) {
            if (strcmp(entries[i].name, select_name) == 0) selected = i;
        }
    }
    if (selected >= entry_count) selected = entry_count - 1;
    if (selected < 0) selected = 0;
}

/* The highlighted entry, or NULL if the list is empty. */
static const Entry *current_entry(void) {
    return entry_count > 0 ? &entries[selected] : NULL;
}

static void draw(void) {
    int rows, cols;
    terminal_size(&rows, &cols);
    int list_rows = rows > 2 ? rows - 2 : 1;

    /* Scroll just enough to keep the highlighted row on screen. */
    if (selected < top) top = selected;
    if (selected >= top + list_rows) top = selected - list_rows + 1;

    ui_begin_frame();
    for (int i = 0; i < list_rows; i++) {
        int index = top + i;
        const Theme *theme = theme_current();
        ui_start_row(i + 1, index == selected ? theme->selected : theme->text);
        if (index < entry_count) {
            ui_put_text(entries[index].name, cols - 1);
            if (entries[index].is_dir) putchar('/');
        }
    }

    char folder[PATH_MAX];
    if (getcwd(folder, sizeof(folder)) == NULL) strcpy(folder, "?");
    char status[PATH_MAX + 64];
    snprintf(status, sizeof(status), "%s theme | %s", theme_current()->name, folder);
    /* A long path may not fit. Show its end: that is the folder you are in. */
    int len = (int)strlen(status);
    ui_draw_bar(status + (len > cols ? len - cols : 0));
    ui_draw_message("Enter open | n new file | f new folder | r rename | d delete | t theme | q quit");
    ui_end_frame(0, 0);
}

static void open_selected(void) {
    const Entry *entry = current_entry();
    if (entry == NULL) return;

    if (entry->is_dir) {
        /* Check first that we will be able to list the folder once inside. */
        if (access(entry->name, R_OK | X_OK) != 0 || chdir(entry->name) != 0) {
            ui_set_message("Cannot open %s: %s", entry->name, strerror(errno));
            return;
        }
        selected = 0;
        top = 0;
        reload(NULL);
    } else if (editor_run(entry->name) != 0) {
        ui_set_message("Cannot open %s: %s", entry->name, strerror(errno));
    }
}

/* Ask for a name for a new item. Returns 1 if we got a usable one. */
static int ask_for_name(const char *label, char *name, size_t size) {
    if (!ui_prompt(label, name, size)) return 0;
    if (!fs_valid_name(name)) {
        ui_set_message("Invalid name: %s", name);
        return 0;
    }
    return 1;
}

static void create_item(int is_folder) {
    char name[256];
    if (!ask_for_name(is_folder ? "New folder name" : "New file name", name, sizeof(name))) return;

    int result = is_folder ? fs_create_folder(name) : fs_create_file(name);
    if (result != 0) {
        ui_set_message("Cannot create %s: %s", name, strerror(errno));
        return;
    }
    ui_set_message("Created %s", name);
    reload(name);
}

/* Returns the highlighted entry, unless it is missing or the ".." row, which
 * must never be renamed or deleted. */
static const Entry *entry_to_change(void) {
    const Entry *entry = current_entry();
    if (entry == NULL || strcmp(entry->name, "..") == 0) {
        ui_set_message("Select a file or folder first");
        return NULL;
    }
    return entry;
}

static void rename_selected(void) {
    const Entry *entry = entry_to_change();
    if (entry == NULL) return;

    char label[512];
    snprintf(label, sizeof(label), "Rename %s to", entry->name);
    char name[256];
    if (!ask_for_name(label, name, sizeof(name))) return;

    if (fs_rename(entry->name, name) != 0) {
        ui_set_message("Cannot rename %s: %s", entry->name, strerror(errno));
        return;
    }
    ui_set_message("Renamed to %s", name);
    reload(name);
}

static void delete_selected(void) {
    const Entry *entry = entry_to_change();
    if (entry == NULL) return;

    char question[512];
    snprintf(question, sizeof(question),
             entry->is_dir ? "Delete folder %s and everything in it? y/n" : "Delete file %s? y/n",
             entry->name);
    int key = ui_ask_key(question);
    if (key != 'y' && key != 'Y') {
        ui_set_message("Not deleted");
        return;
    }

    if (fs_delete(entry->name) != 0) {
        ui_set_message("Cannot delete %s: %s", entry->name, strerror(errno));
    } else {
        ui_set_message("Deleted %s", entry->name);
    }
    /* Reload even after a failure: a folder may be partly deleted. */
    reload(NULL);
}

void browser_run(void) {
    reload(NULL);

    for (;;) {
        draw();

        int key = terminal_read_key();
        switch (key) {
            case KEY_UP:
                if (selected > 0) selected--;
                break;
            case KEY_DOWN:
                if (selected < entry_count - 1) selected++;
                break;
            case KEY_ENTER:
                open_selected();
                break;
            case 'n':
                create_item(0);
                break;
            case 'f':
                create_item(1);
                break;
            case 'r':
                rename_selected();
                break;
            case 'd':
                delete_selected();
                break;
            case 't':
                theme_toggle();
                break;
            case 'q':
            case CTRL_KEY('q'):
                fs_free_list(entries, entry_count);
                return;
        }
    }
}
