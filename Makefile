CC ?= cc
AR ?= ar
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
	commands/wc_module.c \
	commands/yes_module.c \
	src/core.c \
	src/executor.c \
	src/host_linux.c \
	src/programs.c \
	src/ramfs.c \
	src/shell.c \
	src/vfs.c

PROGRAM_SOURCES := src/main.c $(CORE_SOURCES)
TEST_SOURCES := tests/test_core.c $(CORE_SOURCES)
WC_COMMAND_OBJECT := $(BUILD)/wc_command.o
YES_COMMAND_OBJECT := $(BUILD)/netbsd_yes.o
LIBC_OBJECT := $(BUILD)/cb_libc.o
NETBSD_STRLEN_OBJECT := $(BUILD)/netbsd_strlen.o
NETBSD_STRCMP_OBJECT := $(BUILD)/netbsd_strcmp.o
LIBC_OBJECTS := $(LIBC_OBJECT) $(NETBSD_STRLEN_OBJECT) $(NETBSD_STRCMP_OBJECT)
LIBC_ARCHIVE := $(BUILD)/libcannedbsd.a

.PHONY: all clean test sanitize analyze ci check-architecture check-build-modes check-publication print-program

all: $(PROGRAM)

print-program:
	@printf '%s\n' '$(PROGRAM)'

$(BUILD):
	mkdir -p $(BUILD)

$(WC_COMMAND_OBJECT): commands/wc.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/fcntl.h libc/include/stdlib.h \
		libc/include/errno.h libc/include/string.h libc/include/unistd.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_wc_main \
		-c commands/wc.c -o $@

$(YES_COMMAND_OBJECT): upstream/netbsd/usr.bin/yes/yes.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/stdio.h libc/include/stdlib.h libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_yes_main \
		-c upstream/netbsd/usr.bin/yes/yes.c -o $@

$(LIBC_OBJECT): libc/cb_libc.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c libc/cb_libc.c -o $@

$(NETBSD_STRLEN_OBJECT): upstream/netbsd/common/lib/libc/string/strlen.c \
		compat/netbsd/include/assert.h include/cannedbsd/libc.h libc/include/string.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-Dstrlen=cb_libc_strlen -c $< -o $@

$(NETBSD_STRCMP_OBJECT): upstream/netbsd/common/lib/libc/string/strcmp.c \
		compat/netbsd/include/assert.h include/cannedbsd/libc.h libc/include/string.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-DCANNEDBSD_BUILDING_LIBC_STRCMP -c $< -o $@

$(LIBC_ARCHIVE): $(LIBC_OBJECTS)
	$(AR) rcs $@ $^

$(PROGRAM): $(PROGRAM_SOURCES) $(WC_COMMAND_OBJECT) $(YES_COMMAND_OBJECT) $(LIBC_ARCHIVE) include/cannedbsd/abi.h src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PROGRAM_SOURCES) $(WC_COMMAND_OBJECT) \
		$(YES_COMMAND_OBJECT) \
		$(LIBC_ARCHIVE) $(LDFLAGS) -o $@ $(LDLIBS)

$(TEST_PROGRAM): $(TEST_SOURCES) $(WC_COMMAND_OBJECT) $(YES_COMMAND_OBJECT) $(LIBC_ARCHIVE) include/cannedbsd/abi.h src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) $(WC_COMMAND_OBJECT) \
		$(YES_COMMAND_OBJECT) \
		$(LIBC_ARCHIVE) $(LDFLAGS) -o $@ $(LDLIBS)

check-architecture:
	@! rg -n '\b(fork|vfork|execve|posix_spawn|system|popen)[[:space:]]*\(' src include \
		-g '!host_linux.c' || { echo 'forbidden host process API found'; exit 1; }
	@! rg -n '#include[[:space:]]*<((sys/|linux/)|unistd\.h)' src include \
		-g '!host_linux.c' || { echo 'host header leaked outside backend'; exit 1; }
	tests/test_architecture.sh

check-build-modes:
	tests/test_build_modes.sh

check-publication:
	tests/test_publication.sh

test: $(PROGRAM) $(TEST_PROGRAM) check-architecture
	$(TEST_PROGRAM)
	PROGRAM_PATH='$(PROGRAM)' tests/test_launcher.sh
	PROGRAM_PATH='$(PROGRAM)' tests/test_one_process.sh
	BUILD_PATH='$(BUILD)' tests/test_libc_source.sh
	BUILD_PATH='$(BUILD)' tests/test_netbsd_source.sh
	BUILD_PATH='$(BUILD)' tests/test_netbsd_libc_source.sh
	@output="$$( $(PROGRAM) -c 'echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result' )"; \
		test "$$output" = HELLO || { printf 'acceptance output: <%s>\n' "$$output"; exit 1; }
	$(PROGRAM) -c 'false; echo $$?'
	$(PROGRAM) -c 'echo abc | cat | tr a-z A-Z'
	$(PROGRAM) -c 'echo one > /tmp/x; echo two >> /tmp/x; cat /tmp/x'
	$(PROGRAM) -c 'cd /tmp; pwd'
	@output="$$( $(PROGRAM) -c 'echo -n hello | wc -c' )"; \
		test "$$output" = 5 || { printf 'libc acceptance output: <%s>\n' "$$output"; exit 1; }

sanitize:
	$(MAKE) clean
	$(MAKE) CC='$(SANITIZE_CC)' BUILD_VARIANT=sanitize CFLAGS='-std=c99 -Wall -Wextra -Werror -Wpedantic -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined' LDFLAGS='-fsanitize=address,undefined' test

analyze:
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only libc/cb_libc.c \
		$(sort $(PROGRAM_SOURCES) $(TEST_SOURCES))
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_wc_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only commands/wc.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_yes_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/usr.bin/yes/yes.c
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include \
		-Dstrlen=cb_libc_strlen \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/common/lib/libc/string/strlen.c
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include \
		-DCANNEDBSD_BUILDING_LIBC_STRCMP \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/common/lib/libc/string/strcmp.c

ci:
	$(MAKE) check-publication
	$(MAKE) clean test
	$(MAKE) sanitize
	$(MAKE) check-build-modes
	$(MAKE) clean test
	$(MAKE) analyze

clean:
	rm -rf $(BUILD_ROOT)
