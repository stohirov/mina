# simple-code-editor

A small text editor for the terminal, written in C to learn C. It uses only the
C standard library and POSIX, so there is nothing to install. It is tested on
macOS and should also work on Linux, but it won't build on Windows outside WSL.

## Build and run

```sh
make            # build ./editor
make test       # run the tests
make clean      # remove what was built

./editor            # browse the current folder
./editor some/dir   # browse that folder
./editor notes.txt  # edit that file
```

## Keys

File browser:

| Key      | Action                                  |
| -------- | --------------------------------------- |
| Up, Down | Move                                    |
| Enter    | Open a file or go into a folder         |
| `n`      | New file                                |
| `f`      | New folder                              |
| `r`      | Rename                                  |
| `d`      | Delete (a folder goes with its contents) |
| `t`      | Switch between dark and light theme     |
| `q`      | Quit                                    |

Editor:

| Key                 | Action                        |
| ------------------- | ----------------------------- |
| Arrows, Home, End   | Move the cursor               |
| Page Up, Page Down  | Move one screen               |
| Ctrl-S              | Save                          |
| Ctrl-Q              | Close (asks if you have unsaved changes) |
| Ctrl-T              | Switch between dark and light theme |

## Reading the code

Each file does one job. This order goes from simplest to hardest:

| File                | What it does                      | What you learn                         |
| ------------------- | --------------------------------- | -------------------------------------- |
| `src/theme.c`       | The two color themes              | Structs, pointers, `static`            |
| `src/main.c`        | Command line and startup          | `argc`/`argv`, `errno`                 |
| `src/fs.c`          | File and folder operations        | `FILE *`, directories, recursion       |
| `src/buffer.c`      | The text, as an array of lines    | Heap memory, `realloc`, `memmove`      |
| `tests/test.c`      | Tests for the two files above     | Macros, testing without a framework    |
| `src/ui.c`          | Status bar, messages, prompts     | Escape codes, variadic functions       |
| `src/browser.c`     | The file list screen              | Program state, event loops             |
| `src/editor.c`      | The editing screen                | Cursor and scrolling logic             |
| `src/terminal.c`    | Raw mode and reading keys         | `termios`, parsing bytes               |

## Limitations

These keep the code short, and make good exercises:

- One byte counts as one column. Non-ASCII text is stored and displayed, but the
  cursor drifts on those lines and Backspace removes one byte, not one letter.
- The theme resets to dark on every start. Try saving it to a file.
- After resizing the window, the screen is redrawn on the next key press.
- There is no undo, search, copy and paste, or syntax highlighting.
- Saving overwrites the file in place, so a crash mid-save can damage it. Try
  writing to a temporary file and renaming it.

## If the terminal looks broken

A crash, which is likely while you experiment, can leave the terminal in raw
mode. Type `reset` and press Enter, even if you cannot see what you type.
