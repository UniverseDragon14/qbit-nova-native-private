CC ?= cc
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude
LDLIBS ?= -lm

SRC := src/main.c src/util.c src/sha256.c src/lexer.c src/parser.c src/qir.c src/guard.c src/approval.c src/qbc.c src/vm.c
OBJ := $(SRC:src/%.c=build/%.o)
BIN := build/qnova

.PHONY: all clean test install

all: $(BIN)

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDLIBS)

test: $(BIN)
	bash tests/run_tests.sh

install: $(BIN)
	install -m 0755 $(BIN) $(DESTDIR)/usr/local/bin/qnova

clean:
	rm -rf build
