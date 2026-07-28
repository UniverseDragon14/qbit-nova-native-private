CC ?= cc
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror
PKG_CONFIG ?= pkg-config

OPENSSL_CFLAGS := $(shell $(PKG_CONFIG) --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell $(PKG_CONFIG) --libs openssl 2>/dev/null)

CPPFLAGS ?= -Iinclude
CPPFLAGS += $(OPENSSL_CFLAGS)

LDLIBS ?= -lm
LDLIBS += $(OPENSSL_LIBS)

SRC := src/main.c src/util.c src/sha256.c src/lexer.c src/parser.c \
       src/qir.c src/guard.c src/approval.c src/ed25519.c \
       src/signed_approval.c src/trust_store.c src/trust_store_file.c \
       src/qbc.c src/vm.c
OBJ := $(SRC:src/%.c=build/%.o)
BIN := build/qnova
TRUST_TEST := build/test_trust_store
TRUST_FILE_TEST := build/test_trust_store_file

.PHONY: all clean test check-deps install

all: check-deps $(BIN)

check-deps:
	@$(PKG_CONFIG) --exists openssl || { \
		echo "ERROR: OpenSSL development package not found"; \
		exit 1; \
	}

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDLIBS)

$(TRUST_TEST): tests/test_trust_store.c src/trust_store.c src/ed25519.c \
                  src/sha256.c src/util.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		tests/test_trust_store.c src/trust_store.c \
		src/ed25519.c src/sha256.c src/util.c \
		-o $@ $(LDLIBS)

$(TRUST_FILE_TEST): tests/test_trust_store_file.c src/trust_store.c \
                    src/trust_store_file.c src/ed25519.c \
                    src/sha256.c src/util.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		tests/test_trust_store_file.c src/trust_store.c \
		src/trust_store_file.c src/ed25519.c \
		src/sha256.c src/util.c \
		-o $@ $(LDLIBS)

test: check-deps $(BIN) $(TRUST_TEST) $(TRUST_FILE_TEST)
	./$(TRUST_TEST)
	./$(TRUST_FILE_TEST)
	bash tests/run_tests.sh

install: check-deps $(BIN)
	install -m 0755 $(BIN) $(DESTDIR)/usr/local/bin/qnova

clean:
	rm -rf build
