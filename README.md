# simple-code-editor

A small desktop text editor written in C: a folder tree on the left and the text
on the right.

## Download

Ready-to-run builds are on the
[Releases page](https://github.com/stohirov/simple-code-editor/releases/latest),
with nothing else to install. Windows is not supported.

macOS (Apple Silicon and Intel, 11 or newer):

```sh
curl -fL https://github.com/stohirov/simple-code-editor/releases/latest/download/simple-code-editor-macos-universal.tar.gz | tar -xz
cd simple-code-editor-macos-universal
./editor
```

The build is not signed by Apple. If you download it with a browser instead of
`curl`, macOS will refuse to open it until you run
`xattr -d com.apple.quarantine editor`.

Linux (x86-64, needs a desktop with X11 or Wayland and the DejaVu or Liberation
Mono font):

```sh
curl -fL https://github.com/stohirov/simple-code-editor/releases/latest/download/simple-code-editor-linux-x86_64.tar.gz | tar -xz
cd simple-code-editor-linux-x86_64
./editor
```

```sh
./editor            # open the current folder
./editor some/dir   # open that folder
./editor notes.txt  # open that file, with its folder in the tree
```

## Build from source

It needs a C compiler and two C libraries, SDL3 for the window and FreeType for
the text. On macOS:

```sh
brew install sdl3 freetype
```

On Linux, install them with your package manager, along with `pkg-config`.

```sh
make            # build ./editor
make test       # run the tests
make clean      # remove what was built
```

## Releasing

`scripts/build-release.sh` builds and tests a self-contained package in `dist/`.
GitHub Actions runs it on every push to `main`. To publish a new version on the
Releases page, push a tag:

```sh
git tag v0.2.0
git push origin v0.2.0
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

Drag with the mouse, or hold Shift while moving the cursor, to select text.

| Key                 | Action               |
| ------------------- | -------------------- |
| Cmd-S or Ctrl-S     | Save                 |
| Cmd-C or Ctrl-C     | Copy the selection   |
| Cmd-V or Ctrl-V     | Paste                |
| Cmd-Z or Ctrl-Z     | Undo                 |
| Cmd-Q or Ctrl-Q     | Quit                 |
| Arrows, Home, End   | Move the cursor      |
| Cmd-Left, Cmd-Right | Line start, line end |
| Page Up, Page Down  | Move one screen      |

Undo goes back up to 1000 steps per open file, and a typed word or a run of
Backspace counts as one step. The history is lost when you open another file.

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
  or pasted in is shown as `?`. Those bytes are still saved unchanged.
- One file is open at a time.
- There is no redo, cut or search.
- Saving overwrites the file in place, so a crash mid-save can damage it.
