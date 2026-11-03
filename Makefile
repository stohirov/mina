CC ?= cc
CFLAGS = -std=gnu11 -Wall -Wextra -g

SRC = src/main.c src/terminal.c src/theme.c src/ui.c src/buffer.c src/fs.c \
      src/browser.c src/editor.c
HDR = $(wildcard src/*.h)

# Tests only need the parts that do not touch the screen.
TEST_SRC = tests/test.c src/buffer.c src/fs.c

editor: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o editor

# The sanitizers make memory mistakes crash loudly instead of hiding.
test: $(TEST_SRC) $(HDR)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -Isrc $(TEST_SRC) -o run_tests
	./run_tests

clean:
	rm -rf editor run_tests *.dSYM

.PHONY: test clean
