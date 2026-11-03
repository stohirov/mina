# simple-code-editor

A small desktop text editor written in C: a folder tree on the left and the text
on the right.

## Setup

It needs a C compiler and two C libraries, SDL3 for the window and FreeType for
the text. On macOS:

```sh
brew install sdl3 freetype
```

Tested on macOS. It should also work on Linux with `pkg-config` and those two
libraries installed.

## Build and run

```sh
make            # build ./editor
make test       # run the tests
make clean      # remove what was built

./editor            # open the current folder
./editor some/dir   # open that folder
./editor notes.txt  # open that file, with its folder in the tree
```

## Using it

Click a file in the tree to open it, and click a folder to open or close it.

| Button     | Action                                                 |
| ---------- | ------------------------------------------------------ |
| New File   | Create a file in the selected folder and open it       |
| New Folder | Create a folder in the selected folder                 |
| Rename     | Rename the selected file or folder                     |
| Delete     | Delete the selected file, or folder with its contents  |
| Save       | Save the open file                                     |
| Theme      | Switch between dark and light                          |

Click empty space in the tree to clear the selection, so new items go in the top
folder. Questions appear in the status bar at the bottom: type a name and press
Enter, or answer `y` or `n`. Esc cancels.

| Key                       | Action              |
| ------------------------- | ------------------- |
| Cmd-S or Ctrl-S           | Save                |
| Cmd-Q or Ctrl-Q           | Quit                |
| Arrows, Home, End         | Move the cursor     |
| Cmd-Left, Cmd-Right       | Line start, line end |
| Page Up, Page Down        | Move one screen     |

## Code

| File                | Purpose                                        |
| ------------------- | ---------------------------------------------- |
| `src/main.c`        | Startup, window and event loop                 |
| `src/app.c`         | Toolbar, tree, text area, status bar and input |
| `src/editor.c`      | The open file: editing, cursor and scrolling   |
| `src/buffer.c`      | The text, as an array of lines                 |
| `src/tree.c`        | The rows of the folder tree                    |
| `src/fs.c`          | File and folder operations                     |
| `src/font.c`        | Drawing text                                   |
| `src/theme.c`       | The dark and light colors                      |
| `tests/test.c`      | Tests for everything that does not draw        |
| `tests/test_app.c`  | Tests that send the app events, with no window |

## Limitations

- English only: you can type printable ASCII, and any other character in a file
  is shown as `?`. Those bytes are still saved unchanged.
- One file is open at a time.
- There is no selection, copy and paste, undo or search.
- Saving overwrites the file in place, so a crash mid-save can damage it.
