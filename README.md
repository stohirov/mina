# mina

A small desktop text editor written in C: a folder tree on the left and the text
on the right.

## Install

### Mac app

This gives you Mina in Applications, Launchpad and the Dock. It works on Apple
Silicon and Intel, on macOS 11 or newer:

```sh
curl -fLO https://github.com/stohirov/mina/releases/latest/download/mina-macos-app.zip
unzip -q mina-macos-app.zip
mv Mina.app /Applications/
```

Mina is not signed by Apple, so if you download the zip with a browser instead,
macOS refuses to open it at first. Try to open it once, then choose **Open Anyway**
in System Settings, under Privacy & Security.

The app opens in your home folder. Click **Open Folder** to work in another one.

### Homebrew

This gives you a `mina` command to use in a terminal, for any folder:

```sh
brew install stohirov/tap/mina
mina .
```

## Download

Ready-to-run builds are on the
[Releases page](https://github.com/stohirov/mina/releases/latest),
with nothing else to install. Windows is not supported.

macOS (Apple Silicon and Intel, 11 or newer):

```sh
curl -fL https://github.com/stohirov/mina/releases/latest/download/mina-macos-universal.tar.gz | tar -xz
cd mina-macos-universal
./mina
```

The build is not signed by Apple. If you download it with a browser instead of
`curl`, macOS will refuse to open it until you run
`xattr -d com.apple.quarantine mina`.

Linux (x86-64, needs a desktop with X11 or Wayland and the DejaVu or Liberation
Mono font):

```sh
curl -fL https://github.com/stohirov/mina/releases/latest/download/mina-linux-x86_64.tar.gz | tar -xz
cd mina-linux-x86_64
./mina
```

```sh
./mina            # open the current folder
./mina some/dir   # open that folder
./mina notes.txt  # open that file, with its folder in the tree
```

## Build from source

It needs a C compiler and two C libraries, SDL3 for the window and FreeType for
the text. On macOS:

```sh
brew install sdl3 freetype
```

On Linux, install them with your package manager, along with `pkg-config`.

```sh
make            # build ./mina
make test       # run the tests
make app        # build Mina.app, on macOS
make clean      # remove what was built
```

## Releasing

`scripts/build-release.sh` builds and tests a self-contained package in `dist/`.
GitHub Actions runs it on every push to `main`. To publish a new version, push a
tag. The builds then appear on the Releases page, and the Homebrew formula in
[homebrew-tap](https://github.com/stohirov/homebrew-tap) moves to that version:

```sh
git tag v0.3.0
git push origin v0.3.0
```

Updating the formula needs the `HOMEBREW_TAP_DEPLOY_KEY` secret, which
`scripts/setup-tap-deploy-key.sh` creates.

## Using it

Click a file in the tree to open it in a tab, and click a folder to open or close
it. To read a long name, scroll the tree sideways with a trackpad, or hold Shift
and use the mouse wheel. Right-click a file or folder for this menu. Right-clicking empty space in the
tree offers the first two, for the top folder.

| Menu item  | Action                                                       |
| ---------- | ------------------------------------------------------------ |
| New File   | Create a file there and open it                              |
| New Folder | Create a folder there                                        |
| Rename     | Rename the file or folder                                    |
| Delete     | Delete the file, or the folder with its contents             |

Questions appear in the status bar at the bottom: type a name and press Enter, or
answer `y` or `n`. Esc cancels. Open Folder, Save and Theme are in the top right
corner. Opening a folder closes the open tabs, and asks first about unsaved ones.

Drag with the mouse, or hold Shift while moving the cursor, to select text.

| Key                 | Action                 |
| ------------------- | ---------------------- |
| Cmd-O or Ctrl-O     | Open a folder          |
| Cmd-S or Ctrl-S     | Save                   |
| Cmd-C or Ctrl-C     | Copy the selection     |
| Cmd-V or Ctrl-V     | Paste                  |
| Cmd-Z or Ctrl-Z     | Undo                   |
| Cmd-W or Ctrl-W     | Close the tab          |
| Ctrl-Tab            | Next tab               |
| Ctrl-Shift-Tab      | Previous tab           |
| Cmd-Q or Ctrl-Q     | Quit                   |
| Arrows, Home, End   | Move the cursor        |
| Cmd-Left, Cmd-Right | Line start, line end   |
| Page Up, Page Down  | Move one screen        |

Each tab has its own undo history of up to 1000 steps, and a typed word or a run
of Backspace counts as one step. The history is lost when the tab closes. If tabs
do not all fit, scroll over them with the mouse wheel.

## Code

| File                | Purpose                                        |
| ------------------- | ---------------------------------------------- |
| `src/main.c`        | Startup, window and event loop                 |
| `src/app.c`         | Tree, tabs, menu, text area and status bar     |
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
- There is no redo, cut or search.
- Saving overwrites the file in place, so a crash mid-save can damage it.

## License

MIT. See [LICENSE](LICENSE). The downloadable builds also include the libraries
listed in [THIRD_PARTY.md](THIRD_PARTY.md).
