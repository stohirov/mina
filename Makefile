CC ?= cc
CFLAGS = -std=gnu11 -Wall -Wextra -g

ifneq ($(shell which pkg-config 2>/dev/null),)
  DEP_CFLAGS := $(shell pkg-config --cflags sdl3 freetype2)
  DEP_LIBS := $(shell pkg-config --libs sdl3 freetype2)
else
  BREW := $(shell brew --prefix)
  DEP_CFLAGS := -I$(BREW)/include -I$(BREW)/include/freetype2
  DEP_LIBS := -L$(BREW)/lib -lSDL3 -lfreetype
endif

CORE = src/util.c src/buffer.c src/fs.c src/tree.c src/editor.c
UI = src/theme.c src/font.c src/app.c
HDR = $(wildcard src/*.h tests/*.h)
SANITIZE = -fsanitize=address,undefined

mina: src/main.c $(CORE) $(UI) $(HDR)
	$(CC) $(CFLAGS) $(DEP_CFLAGS) src/main.c $(CORE) $(UI) $(DEP_LIBS) -o mina

test: tests/test.c tests/test_app.c $(CORE) $(UI) $(HDR)
	$(CC) $(CFLAGS) $(SANITIZE) -Isrc tests/test.c $(CORE) -o run_tests
	$(CC) $(CFLAGS) $(SANITIZE) $(DEP_CFLAGS) -Isrc tests/test_app.c $(CORE) $(UI) $(DEP_LIBS) -o run_app_tests
	./run_tests
	./run_app_tests

clean:
	rm -rf mina run_tests run_app_tests *.dSYM

.PHONY: test clean
