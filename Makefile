CC ?= cc
CPPFLAGS ?= -D_XOPEN_SOURCE=700 -Iinclude -Isrc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O2
LDFLAGS ?=
LDLIBS ?=
SANITIZE_CC ?= $(CC)

BUILD_ROOT := build
BUILD_VARIANT ?= normal
ifeq ($(BUILD_VARIANT),normal)
BUILD := $(BUILD_ROOT)
else
BUILD := $(BUILD_ROOT)/$(BUILD_VARIANT)
endif
PROGRAM := $(BUILD)/bsdinacan
TEST_PROGRAM := $(BUILD)/test_core

CORE_SOURCES := \
	src/core.c \
	src/fs.c \
	src/host_linux.c \
	src/programs.c \
	src/shell.c

PROGRAM_SOURCES := src/main.c $(CORE_SOURCES)
TEST_SOURCES := tests/test_core.c $(CORE_SOURCES)

.PHONY: all clean test sanitize analyze ci check-architecture check-build-modes check-publication print-program

all: $(PROGRAM)

print-program:
	@printf '%s\n' '$(PROGRAM)'

$(BUILD):
	mkdir -p $(BUILD)

$(PROGRAM): $(PROGRAM_SOURCES) include/cannedbsd/abi.h src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PROGRAM_SOURCES) $(LDFLAGS) -o $@ $(LDLIBS)

$(TEST_PROGRAM): $(TEST_SOURCES) include/cannedbsd/abi.h src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) $(LDFLAGS) -o $@ $(LDLIBS)

check-architecture:
	@! rg -n '\b(fork|vfork|execve|posix_spawn|system|popen)[[:space:]]*\(' src include \
		-g '!host_linux.c' || { echo 'forbidden host process API found'; exit 1; }
	@! rg -n '#include[[:space:]]*<((sys/|linux/)|unistd\.h)' src include \
		-g '!host_linux.c' || { echo 'host header leaked outside backend'; exit 1; }

check-build-modes:
	tests/test_build_modes.sh

check-publication:
	tests/test_publication.sh

test: $(PROGRAM) $(TEST_PROGRAM) check-architecture
	$(TEST_PROGRAM)
	PROGRAM_PATH='$(PROGRAM)' tests/test_launcher.sh
	@output="$$( $(PROGRAM) -c 'echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result' )"; \
		test "$$output" = HELLO || { printf 'acceptance output: <%s>\n' "$$output"; exit 1; }
	$(PROGRAM) -c 'false; echo $$?'
	$(PROGRAM) -c 'echo abc | cat | tr a-z A-Z'
	$(PROGRAM) -c 'echo one > /tmp/x; echo two >> /tmp/x; cat /tmp/x'
	$(PROGRAM) -c 'cd /tmp; pwd'

sanitize:
	$(MAKE) clean
	$(MAKE) CC='$(SANITIZE_CC)' BUILD_VARIANT=sanitize CFLAGS='-std=c99 -Wall -Wextra -Werror -Wpedantic -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined' LDFLAGS='-fsanitize=address,undefined' test

analyze:
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only $(sort $(PROGRAM_SOURCES) $(TEST_SOURCES))

ci:
	$(MAKE) check-publication
	$(MAKE) clean test
	$(MAKE) sanitize
	$(MAKE) check-build-modes
	$(MAKE) clean test
	$(MAKE) analyze

clean:
	rm -rf $(BUILD_ROOT)
