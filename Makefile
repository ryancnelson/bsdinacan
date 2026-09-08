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
	commands/printenv_module.c commands/dirname_module.c \
	commands/basename_module.c \
	commands/echo_module.c \
	src/core.c \
	src/executor.c \
	src/host_linux.c \
	src/programs.c \
	src/ramfs.c \
	src/shell.c \
	src/vfs.c

PROGRAM_SOURCES := src/main.c $(CORE_SOURCES)
TEST_SOURCES := tests/libc_fwrite_compat_module.c tests/test_fwrite.c tests/libc_fwrite_probe_module.c tests/test_fread.c tests/libc_fread_probe_module.c tests/libc_fread_compat_module.c tests/test_file.c tests/libc_file_probe_module.c tests/libc_file_compat_module.c tests/test_stdin.c tests/libc_stdin_probe_module.c tests/libc_stdin_compat_module.c tests/test_getopt_arg.c tests/libc_getopt_arg_probe_module.c tests/test_echo_state.c tests/test_argv.c tests/libc_argv_probe_module.c tests/test_stdio_state.c tests/libc_stdio_state_probe_module.c tests/libc_stdio_oldtable_probe_module.c tests/vfs_executable_probe.c tests/libc_progname_probe_module.c tests/test_core.c tests/test_locale.c tests/libc_locale_probe_module.c tests/test_terminal.c tests/libc_terminal_probe_module.c tests/libc_memory_probe_module.c tests/libc_exit_probe_module.c tests/libc_getopt_probe_module.c tests/libc_truncate_probe_module.c tests/libc_errx_probe_module.c tests/libc_err_probe_module.c tests/libc_warn_probe_module.c tests/libc_strcpy_probe_module.c tests/libc_dirname_probe_module.c tests/libc_dirent_probe_module.c tests/libc_basename_probe_module.c tests/libc_strtoimax_probe_module.c $(CORE_SOURCES)
WC_COMMAND_OBJECT := $(BUILD)/wc_command.o
YES_COMMAND_OBJECT := $(BUILD)/netbsd_yes.o
PRINTENV_COMMAND_OBJECT := $(BUILD)/netbsd_printenv.o
DIRNAME_COMMAND_OBJECT := $(BUILD)/dirname_command.o
BASENAME_COMMAND_OBJECT := $(BUILD)/basename_command.o
ECHO_COMMAND_OBJECT := $(BUILD)/netbsd_echo.o
EXITPROBE_COMMAND_OBJECT := $(BUILD)/exitprobe_command.o
GETOPTPROBE_COMMAND_OBJECT := $(BUILD)/getoptprobe_command.o
ERRXPROBE_COMMAND_OBJECT := $(BUILD)/errxprobe_command.o
ERRPROBE_COMMAND_OBJECT := $(BUILD)/errprobe_command.o
STRCPYPROBE_COMMAND_OBJECT := $(BUILD)/strcpyprobe_command.o
PROGNAMEPROBE_COMMAND_OBJECT := $(BUILD)/prognameprobe_command.o
DIRNAMEPROBE_COMMAND_OBJECT := $(BUILD)/dirnameprobe_command.o
DIRNAME_OLDTABLE_TEST_OBJECT := $(BUILD)/libc_dirname_oldtable_probe.o
STDIO_OLDTABLE_TEST_OBJECT := $(BUILD)/libc_stdio_oldtable_probe.o
BASENAMEPROBE_COMMAND_OBJECT := $(BUILD)/basenameprobe_command.o
STRTOIMAXPROBE_COMMAND_OBJECT := $(BUILD)/strtoimaxprobe_command.o
BASENAME_OLDTABLE_TEST_OBJECT := $(BUILD)/libc_basename_oldtable_probe.o
DIRENTPROBE_COMMAND_OBJECT := $(BUILD)/direntprobe_command.o
DIRENT_OLDTABLE_TEST_OBJECT := $(BUILD)/libc_dirent_oldtable_probe.o
DIRENT_ALLOCFAIL_TEST_OBJECT := $(BUILD)/libc_dirent_allocfail_probe.o
DIRENT_READDIR_UNAVAIL_TEST_OBJECT := $(BUILD)/libc_dirent_readdir_unavailable_probe.o
DIRENT_CLOSEDIR_REBIND_TEST_OBJECT := $(BUILD)/libc_dirent_closedir_rebind_probe.o
LIBC_OBJECT := $(BUILD)/cb_libc.o
NETBSD_STRLEN_OBJECT := $(BUILD)/netbsd_strlen.o
NETBSD_STRCMP_OBJECT := $(BUILD)/netbsd_strcmp.o
NETBSD_STRCPY_OBJECT := $(BUILD)/netbsd_strcpy.o
NETBSD_MEMCPY_OBJECT := $(BUILD)/netbsd_memcpy.o
NETBSD_MEMMOVE_OBJECT := $(BUILD)/netbsd_memmove.o
NETBSD_MEMCMP_OBJECT := $(BUILD)/netbsd_memcmp.o
NETBSD_STRCHR_OBJECT := $(BUILD)/netbsd_strchr.o
NETBSD_DIRNAME_OBJECT := $(BUILD)/netbsd_dirname.o
NETBSD_BASENAME_OBJECT := $(BUILD)/netbsd_basename.o
NETBSD_STRTOIMAX_OBJECT := $(BUILD)/netbsd_strtoimax.o
LIBC_ALLOCATION_TEST_OBJECT := $(BUILD)/libc_allocation_source.o
LIBC_MEMORY_TEST_OBJECT := $(BUILD)/libc_memory_source.o
LIBC_ENVIRON_TEST_OBJECT := $(BUILD)/libc_environ_source.o
LIBC_STDIO_TEST_OBJECT := $(BUILD)/libc_stdio_source.o
LIBC_MEMORY_PROBE_OBJECT := $(BUILD)/libc_memory_probe.o
LIBC_STDIO_STATE_PROBE_OBJECT := $(BUILD)/libc_stdio_state_probe.o
LIBC_TRUNCATE_TEST_OBJECT := $(BUILD)/libc_truncate_probe.o
LIBC_EXEC_ERRNO_TEST_OBJECT := $(BUILD)/libc_exec_errno_probe.o
LIBC_LOCALE_TEST_OBJECT := $(BUILD)/libc_locale_probe.o
LIBC_TERMINAL_TEST_OBJECT := $(BUILD)/libc_terminal_probe.o
LIBC_POLL_TEST_OBJECT := $(BUILD)/libc_poll_source.o
LIBC_OBJECTS := $(LIBC_OBJECT) $(NETBSD_STRLEN_OBJECT) \
	$(NETBSD_STRCMP_OBJECT) $(NETBSD_MEMCPY_OBJECT) $(NETBSD_MEMMOVE_OBJECT) \
	$(NETBSD_MEMCMP_OBJECT)

LIBC_OBJECTS += $(NETBSD_STRCHR_OBJECT)
LIBC_OBJECTS += $(NETBSD_STRCPY_OBJECT)
LIBC_OBJECTS += $(NETBSD_DIRNAME_OBJECT)
LIBC_OBJECTS += $(NETBSD_BASENAME_OBJECT)
LIBC_OBJECTS += $(NETBSD_STRTOIMAX_OBJECT)
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

# -Wno-strict-prototypes: the pinned source's local `extern char **environ;`
# expands, once environ is cannedBSD's task-local accessor macro, into a
# declarator both GCC and Clang parse as an unprototyped redeclaration of
# cb_libc_environ_location. See UPSTREAM.md's printenv entry. No other file
# loses -Wstrict-prototypes coverage.
$(DIRNAME_COMMAND_OBJECT): upstream/netbsd/usr.bin/dirname/dirname.c \
		libc/include/libgen.h libc/include/locale.h \
		include/cannedbsd/libc.h libc/include/stdlib.h libc/include/stdio.h \
		libc/include/unistd.h libc/include/err.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_dirname_main \
		-c $< -o $@

$(BASENAME_COMMAND_OBJECT): upstream/netbsd/usr.bin/basename/basename.c \
		libc/include/libgen.h libc/include/locale.h libc/include/string.h \
		include/cannedbsd/libc.h libc/include/stdlib.h libc/include/stdio.h \
		libc/include/unistd.h libc/include/err.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_basename_main \
		-c $< -o $@

# -Wno-unused-parameter: the pinned source's main is historically marked
# /* ARGSUSED */ (a lint-only annotation, not a compiler flag) because it
# never reads argc, only walking argv until the first NULL. See
# UPSTREAM.md's echo entry. No other file loses -Wunused-parameter coverage.
$(ECHO_COMMAND_OBJECT): upstream/netbsd/bin/echo/echo.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/err.h libc/include/locale.h libc/include/stdio.h \
		libc/include/stdlib.h libc/include/string.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Wno-unused-parameter \
		-Dmain=cb_netbsdecho_main \
		-c upstream/netbsd/bin/echo/echo.c -o $@

$(PRINTENV_COMMAND_OBJECT): upstream/netbsd/usr.bin/printenv/printenv.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/stdio.h libc/include/stdlib.h libc/include/string.h \
		libc/include/unistd.h libc/include/err.h libc/include/sys/cdefs.h \
		libc/include/sys/types.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Wno-strict-prototypes \
		-Dmain=cb_printenv_main \
		-c upstream/netbsd/usr.bin/printenv/printenv.c -o $@

WARNPROBE_COMMAND_OBJECT := $(BUILD)/warnprobe_command.o

$(WARNPROBE_COMMAND_OBJECT): tests/libc_warn_probe.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/errno.h libc/include/err.h \
		libc/include/unistd.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_warn_probe_main \
		-c tests/libc_warn_probe.c -o $@

$(STRCPYPROBE_COMMAND_OBJECT): tests/libc_strcpy_probe.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_strcpy_probe_main \
		-c tests/libc_strcpy_probe.c -o $@

$(ERRPROBE_COMMAND_OBJECT): tests/libc_err_probe.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/err.h libc/include/errno.h \
		libc/include/unistd.h libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_err_probe_main \
		-c $< -o $@

$(PROGNAMEPROBE_COMMAND_OBJECT): tests/libc_progname_probe.c libc/include/stdlib.h include/cannedbsd/libc.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_progname_probe_main \
		-c tests/libc_progname_probe.c -o $@

$(DIRNAMEPROBE_COMMAND_OBJECT): tests/libc_dirname_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/libgen.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_dirname_probe_main \
		-c tests/libc_dirname_probe.c -o $@

$(DIRNAME_OLDTABLE_TEST_OBJECT): tests/libc_dirname_oldtable_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/libgen.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_dirname_oldtable_main \
		-c tests/libc_dirname_oldtable_probe.c -o $@

$(BASENAMEPROBE_COMMAND_OBJECT): tests/libc_basename_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/libgen.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_basename_probe_main \
		-c tests/libc_basename_probe.c -o $@

$(STRTOIMAXPROBE_COMMAND_OBJECT): tests/libc_strtoimax_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/ctype.h libc/include/errno.h libc/include/inttypes.h \
		libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_strtoimax_probe_main \
		-c tests/libc_strtoimax_probe.c -o $@

$(BASENAME_OLDTABLE_TEST_OBJECT): tests/libc_basename_oldtable_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/libgen.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_basename_oldtable_main \
		-c tests/libc_basename_oldtable_probe.c -o $@

$(EXITPROBE_COMMAND_OBJECT): tests/libc_exit_probe.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/stdlib.h libc/include/unistd.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_exitprobe_main \
		-c tests/libc_exit_probe.c -o $@

$(GETOPTPROBE_COMMAND_OBJECT): tests/libc_getopt_probe.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/string.h libc/include/unistd.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_getoptprobe_main \
		-c tests/libc_getopt_probe.c -o $@

$(ERRXPROBE_COMMAND_OBJECT): tests/libc_errx_probe.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/err.h libc/include/unistd.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_errxprobe_main \
		-c tests/libc_errx_probe.c -o $@

$(DIRENTPROBE_COMMAND_OBJECT): tests/libc_dirent_probe.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/dirent.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_direntprobe_main \
		-c tests/libc_dirent_probe.c -o $@

$(DIRENT_OLDTABLE_TEST_OBJECT): tests/libc_dirent_oldtable_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/dirent.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_direntoldtable_main \
		-c tests/libc_dirent_oldtable_probe.c -o $@

$(DIRENT_ALLOCFAIL_TEST_OBJECT): tests/libc_dirent_allocfail_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/dirent.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_direntallocfail_main \
		-c tests/libc_dirent_allocfail_probe.c -o $@

$(DIRENT_READDIR_UNAVAIL_TEST_OBJECT): tests/libc_dirent_readdir_unavailable_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/dirent.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_direntreaddirunavail_main \
		-c tests/libc_dirent_readdir_unavailable_probe.c -o $@

$(DIRENT_CLOSEDIR_REBIND_TEST_OBJECT): tests/libc_dirent_closedir_rebind_probe.c \
		include/cannedbsd/abi.h include/cannedbsd/libc.h \
		libc/include/dirent.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) \
		-c tests/libc_dirent_closedir_rebind_probe.c -o $@

$(LIBC_OBJECT): libc/cb_libc.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c libc/cb_libc.c -o $@

$(NETBSD_STRLEN_OBJECT): upstream/netbsd/common/lib/libc/string/strlen.c \
		compat/netbsd/include/assert.h include/cannedbsd/libc.h libc/include/string.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-Dstrlen=cb_libc_strlen -c $< -o $@


$(NETBSD_STRCPY_OBJECT): upstream/netbsd/common/lib/libc/string/strcpy.c \
		compat/netbsd/include/assert.h include/cannedbsd/libc.h libc/include/string.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-DCANNEDBSD_BUILDING_LIBC_STRCPY -c $< -o $@

$(NETBSD_STRCMP_OBJECT): upstream/netbsd/common/lib/libc/string/strcmp.c \
		compat/netbsd/include/assert.h include/cannedbsd/libc.h libc/include/string.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-DCANNEDBSD_BUILDING_LIBC_STRCMP -c $< -o $@

$(NETBSD_MEMCPY_OBJECT): upstream/netbsd/common/lib/libc/string/memcpy.c \
		upstream/netbsd/common/lib/libc/string/bcopy.c \
		compat/netbsd/include/assert.h include/cannedbsd/libc.h libc/include/string.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) -Os \
		-DCANNEDBSD_BUILDING_LIBC_MEMCPY -c $< -o $@

$(NETBSD_MEMMOVE_OBJECT): upstream/netbsd/common/lib/libc/string/memmove.c \
		upstream/netbsd/common/lib/libc/string/bcopy.c \
		compat/netbsd/include/assert.h include/cannedbsd/libc.h libc/include/string.h \
		libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) -Os \
		-DCANNEDBSD_BUILDING_LIBC_MEMMOVE -c $< -o $@

$(NETBSD_MEMCMP_OBJECT): upstream/netbsd/common/lib/libc/string/memcmp.c \
		compat/netbsd/include/assert.h compat/netbsd/include/sys/types.h \
		include/cannedbsd/libc.h libc/include/string.h libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-DCANNEDBSD_BUILDING_LIBC_MEMCMP -c $< -o $@

$(NETBSD_STRCHR_OBJECT): upstream/netbsd/common/lib/libc/string/strchr.c \
		compat/netbsd/include/assert.h compat/netbsd/include/namespace.h \
		include/cannedbsd/libc.h libc/include/string.h libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-c $< -o $@

$(NETBSD_DIRNAME_OBJECT): upstream/netbsd/lib/libc/gen/dirname.c \
		compat/netbsd/include/namespace.h compat/netbsd/include/sys/param.h \
		compat/netbsd/include/limits.h compat/netbsd/include/libgen.h \
		include/cannedbsd/libc.h libc/include/string.h libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-Ddirname=cb_libc_dirname_upstream -c $< -o $@

$(NETBSD_BASENAME_OBJECT): upstream/netbsd/lib/libc/gen/basename.c \
		compat/netbsd/include/namespace.h compat/netbsd/include/sys/param.h \
		compat/netbsd/include/limits.h compat/netbsd/include/libgen.h \
		include/cannedbsd/libc.h libc/include/string.h libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-Dbasename=cb_libc_basename_upstream -c $< -o $@

$(NETBSD_STRTOIMAX_OBJECT): upstream/netbsd/common/lib/libc/stdlib/strtoimax.c \
		upstream/netbsd/common/lib/libc/stdlib/_strtol.h \
		compat/netbsd/include/assert.h compat/netbsd/include/nbtool_config.h \
		include/cannedbsd/libc.h libc/include/sys/cdefs.h \
		libc/include/ctype.h libc/include/errno.h libc/include/inttypes.h \
		libc/include/stdlib.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include $(CFLAGS) \
		-DHAVE_NBTOOL_CONFIG_H=1 -Dstrtoimax=cb_libc_strtoimax \
		-c $< -o $@

$(LIBC_ARCHIVE): $(LIBC_OBJECTS)
	$(AR) rcs $@ $^

$(LIBC_ALLOCATION_TEST_OBJECT): tests/libc_allocation_source.c \
		include/cannedbsd/libc.h libc/include/stdlib.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -c $< -o $@

$(LIBC_MEMORY_TEST_OBJECT): tests/libc_memory_source.c \
		include/cannedbsd/libc.h libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -c $< -o $@

$(LIBC_ENVIRON_TEST_OBJECT): tests/libc_environ_source.c \
		include/cannedbsd/libc.h libc/include/unistd.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -c $< -o $@

$(LIBC_STDIO_TEST_OBJECT): tests/libc_stdio_source.c \
		include/cannedbsd/libc.h libc/include/errno.h libc/include/stdio.h \
		libc/include/unistd.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_stdio_test_main \
		-c $< -o $@

$(LIBC_MEMORY_PROBE_OBJECT): tests/libc_memory_probe.c \
        include/cannedbsd/libc.h libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_memory_probe_main \
		-c $< -o $@


$(LIBC_STDIO_STATE_PROBE_OBJECT): tests/libc_stdio_state_probe.c \
        include/cannedbsd/libc.h libc/include/stdio.h libc/include/errno.h libc/include/unistd.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_stdio_state_probe_main \
		-c $< -o $@

$(STDIO_OLDTABLE_TEST_OBJECT): tests/libc_stdio_oldtable_probe.c \
        include/cannedbsd/libc.h libc/include/stdio.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_stdio_oldtable_main \
		-c $< -o $@

$(LIBC_TRUNCATE_TEST_OBJECT): tests/libc_truncate_probe.c \
        include/cannedbsd/libc.h libc/include/unistd.h libc/include/fcntl.h \
        libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_truncate_probe_main \
		-c $< -o $@

$(PROGRAM): $(PROGRAM_SOURCES) $(WC_COMMAND_OBJECT) $(YES_COMMAND_OBJECT) $(PRINTENV_COMMAND_OBJECT) $(DIRNAME_COMMAND_OBJECT) $(BASENAME_COMMAND_OBJECT) $(ECHO_COMMAND_OBJECT) $(LIBC_ARCHIVE) include/cannedbsd/abi.h src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PROGRAM_SOURCES) $(WC_COMMAND_OBJECT) \
		$(YES_COMMAND_OBJECT) $(PRINTENV_COMMAND_OBJECT) $(DIRNAME_COMMAND_OBJECT) $(BASENAME_COMMAND_OBJECT) $(ECHO_COMMAND_OBJECT) \
		$(LIBC_ARCHIVE) $(LDFLAGS) -o $@ $(LDLIBS)

$(TEST_PROGRAM): $(TEST_SOURCES) $(WC_COMMAND_OBJECT) $(YES_COMMAND_OBJECT) $(PRINTENV_COMMAND_OBJECT) $(DIRNAME_COMMAND_OBJECT) $(BASENAME_COMMAND_OBJECT) $(ECHO_COMMAND_OBJECT) \
		$(EXITPROBE_COMMAND_OBJECT) $(BUILD)/libc_getopt_arg_probe.o $(GETOPTPROBE_COMMAND_OBJECT) $(ERRXPROBE_COMMAND_OBJECT) $(ERRPROBE_COMMAND_OBJECT) $(WARNPROBE_COMMAND_OBJECT) $(STRCPYPROBE_COMMAND_OBJECT) $(PROGNAMEPROBE_COMMAND_OBJECT) $(DIRNAMEPROBE_COMMAND_OBJECT) $(DIRNAME_OLDTABLE_TEST_OBJECT) $(DIRENTPROBE_COMMAND_OBJECT) $(DIRENT_OLDTABLE_TEST_OBJECT) $(DIRENT_ALLOCFAIL_TEST_OBJECT) $(DIRENT_READDIR_UNAVAIL_TEST_OBJECT) $(DIRENT_CLOSEDIR_REBIND_TEST_OBJECT) $(BASENAMEPROBE_COMMAND_OBJECT) $(BASENAME_OLDTABLE_TEST_OBJECT) $(STRTOIMAXPROBE_COMMAND_OBJECT) $(LIBC_STDIO_TEST_OBJECT) $(BUILD)/libc_fread_probe.o $(BUILD)/libc_file_probe.o $(BUILD)/libc_stdin_probe.o $(BUILD)/libc_argv_probe.o $(LIBC_STDIO_STATE_PROBE_OBJECT) $(BUILD)/libc_fwrite_probe.o $(BUILD)/libc_fwrite_wrapper_probe.o $(STDIO_OLDTABLE_TEST_OBJECT) $(LIBC_MEMORY_PROBE_OBJECT) $(LIBC_TRUNCATE_TEST_OBJECT) $(LIBC_TERMINAL_TEST_OBJECT) $(LIBC_LOCALE_TEST_OBJECT) $(LIBC_EXEC_ERRNO_TEST_OBJECT) $(LIBC_POLL_TEST_OBJECT) $(LIBC_ARCHIVE) \
		include/cannedbsd/abi.h include/cannedbsd/harness.h platform/mac68k/acceptance_cases.def platform/mac68k/acceptance_output.h src/internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) $(WC_COMMAND_OBJECT) \
		$(YES_COMMAND_OBJECT) $(PRINTENV_COMMAND_OBJECT) $(DIRNAME_COMMAND_OBJECT) $(BASENAME_COMMAND_OBJECT) $(ECHO_COMMAND_OBJECT) $(EXITPROBE_COMMAND_OBJECT) $(BUILD)/libc_getopt_arg_probe.o $(GETOPTPROBE_COMMAND_OBJECT) $(ERRXPROBE_COMMAND_OBJECT) $(ERRPROBE_COMMAND_OBJECT) $(WARNPROBE_COMMAND_OBJECT) $(STRCPYPROBE_COMMAND_OBJECT) $(PROGNAMEPROBE_COMMAND_OBJECT) $(DIRNAMEPROBE_COMMAND_OBJECT) $(DIRNAME_OLDTABLE_TEST_OBJECT) $(DIRENTPROBE_COMMAND_OBJECT) $(DIRENT_OLDTABLE_TEST_OBJECT) $(DIRENT_ALLOCFAIL_TEST_OBJECT) $(DIRENT_READDIR_UNAVAIL_TEST_OBJECT) $(DIRENT_CLOSEDIR_REBIND_TEST_OBJECT) $(BASENAMEPROBE_COMMAND_OBJECT) $(BASENAME_OLDTABLE_TEST_OBJECT) $(STRTOIMAXPROBE_COMMAND_OBJECT) $(LIBC_STDIO_TEST_OBJECT) $(BUILD)/libc_fread_probe.o $(BUILD)/libc_file_probe.o $(BUILD)/libc_stdin_probe.o $(BUILD)/libc_argv_probe.o $(LIBC_STDIO_STATE_PROBE_OBJECT) $(BUILD)/libc_fwrite_probe.o $(BUILD)/libc_fwrite_wrapper_probe.o $(STDIO_OLDTABLE_TEST_OBJECT) $(LIBC_MEMORY_PROBE_OBJECT) $(LIBC_TRUNCATE_TEST_OBJECT) $(LIBC_TERMINAL_TEST_OBJECT) $(LIBC_LOCALE_TEST_OBJECT) $(LIBC_EXEC_ERRNO_TEST_OBJECT) $(LIBC_POLL_TEST_OBJECT) \
		$(LIBC_ARCHIVE) $(LDFLAGS) -o $@ $(LDLIBS)


$(LIBC_EXEC_ERRNO_TEST_OBJECT): tests/libc_exec_errno_probe.c include/cannedbsd/libc.h \
		libc/include/errno.h libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_exec_errno_probe_main -c $< -o $@

$(LIBC_LOCALE_TEST_OBJECT): tests/libc_locale_probe.c include/cannedbsd/libc.h \
		libc/include/locale.h libc/include/errno.h libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_locale_probe_main -c $< -o $@

$(LIBC_TERMINAL_TEST_OBJECT): tests/libc_terminal_probe.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/termios.h libc/include/unistd.h \
		libc/include/errno.h libc/include/fcntl.h libc/include/poll.h libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_terminal_probe_main -c $< -o $@

$(LIBC_POLL_TEST_OBJECT): tests/libc_poll_source.c include/cannedbsd/abi.h \
		include/cannedbsd/libc.h libc/include/poll.h libc/include/unistd.h \
		libc/include/errno.h libc/include/sys/cdefs.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -c tests/libc_poll_source.c -o $@

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

test: $(PROGRAM) $(TEST_PROGRAM) $(LIBC_ALLOCATION_TEST_OBJECT) \
		$(LIBC_MEMORY_TEST_OBJECT) $(LIBC_ENVIRON_TEST_OBJECT) \
		$(LIBC_STDIO_TEST_OBJECT) check-architecture
	$(TEST_PROGRAM)
	CC='$(CC)' LDLIBS='$(LDLIBS)' tests/test_mac_root_dispatch.sh
	CC='$(CC)' tests/test_mac_autorun.sh
	PROGRAM_PATH='$(PROGRAM)' tests/test_launcher.sh
	PROGRAM_PATH='$(PROGRAM)' tests/test_one_process.sh
	BUILD_PATH='$(BUILD)' tests/test_libc_source.sh
	BUILD_PATH='$(BUILD)' tests/test_netbsd_source.sh
	BUILD_PATH='$(BUILD)' tests/test_netbsd_libc_source.sh
	PROGRAM_PATH='$(PROGRAM)' tests/test_printenv_behavior.sh
	PROGRAM_PATH='$(PROGRAM)' tests/test_dirname_behavior.sh
	PROGRAM_PATH='$(PROGRAM)' tests/test_basename_behavior.sh
	PROGRAM_PATH='$(PROGRAM)' tests/test_echo_behavior.sh
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
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_dirname_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/usr.bin/dirname/dirname.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_argv_probe_main \
		$(CFLAGS) -fanalyzer -fsyntax-only tests/libc_argv_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_fread_probe_main $(CFLAGS) -fanalyzer -fsyntax-only tests/libc_fread_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_fwrite_probe_main $(CFLAGS) -fanalyzer -fsyntax-only tests/libc_fwrite_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_file_probe_main $(CFLAGS) -fanalyzer -fsyntax-only tests/libc_file_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_stdin_probe_main $(CFLAGS) -fanalyzer -fsyntax-only tests/libc_stdin_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_stdio_state_probe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_stdio_state_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_stdio_oldtable_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_stdio_oldtable_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_basename_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/usr.bin/basename/basename.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_netbsdecho_main \
		-Wno-unused-parameter \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/bin/echo/echo.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_printenv_main \
		-Wno-strict-prototypes \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/usr.bin/printenv/printenv.c
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include \
		-Dstrlen=cb_libc_strlen \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/common/lib/libc/string/strlen.c
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include \
		-DCANNEDBSD_BUILDING_LIBC_STRCMP \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/common/lib/libc/string/strcmp.c
	$(CC) $(CPPFLAGS) -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_allocation_source.c
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include -Os \
		-DCANNEDBSD_BUILDING_LIBC_MEMCPY \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/common/lib/libc/string/memcpy.c
	$(CC) $(CPPFLAGS) -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_memory_source.c
	$(CC) $(CPPFLAGS) -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_environ_source.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_exitprobe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_exit_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_stdio_source.c

	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_getopt_arg_probe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic -fanalyzer -fsyntax-only tests/libc_getopt_arg_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_getoptprobe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_getopt_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_errxprobe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_errx_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_direntprobe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_dirent_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_direntoldtable_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_dirent_oldtable_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_direntallocfail_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_dirent_allocfail_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_direntreaddirunavail_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_dirent_readdir_unavailable_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_dirent_closedir_rebind_probe.c
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include -Os \
		-DCANNEDBSD_BUILDING_LIBC_MEMMOVE \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/common/lib/libc/string/memmove.c
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include \
		-DCANNEDBSD_BUILDING_LIBC_MEMCMP \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/common/lib/libc/string/memcmp.c
	$(CC) $(CPPFLAGS) -Icompat/netbsd/include -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only upstream/netbsd/common/lib/libc/string/strchr.c
	$(CC) $(CPPFLAGS) -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_truncate_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_err_probe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_err_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_warn_probe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_warn_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_strcpy_probe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_strcpy_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_progname_probe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_progname_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_dirname_probe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_dirname_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_dirname_oldtable_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_dirname_oldtable_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_terminal_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_locale_probe.c
	$(CC) $(CPPFLAGS) -Ilibc/include -Dmain=cb_exec_errno_probe_main \
		-std=c99 -Wall -Wextra -Werror -Wpedantic \
		-fanalyzer -fsyntax-only tests/libc_exec_errno_probe.c

$(BUILD)/test_acceptance_output: tests/test_acceptance_output.c platform/mac68k/acceptance_output.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

.PHONY: check-acceptance-output
check-acceptance-output: $(BUILD)/test_acceptance_output
	$(BUILD)/test_acceptance_output

ci:
	$(MAKE) check-acceptance-output
	python3 tests/test_mac_guest.py
	$(MAKE) check-publication
	$(MAKE) clean test
	$(MAKE) sanitize
	$(MAKE) check-build-modes
	$(MAKE) clean test
	$(MAKE) analyze

clean:
	rm -rf $(BUILD_ROOT)

$(BUILD)/libc_argv_probe.o: tests/libc_argv_probe.c include/cannedbsd/libc.h libc/include/stdlib.h libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_argv_probe_main -c $< -o $@

$(BUILD)/libc_getopt_arg_probe.o: tests/libc_getopt_arg_probe.c include/cannedbsd/libc.h libc/include/unistd.h libc/include/string.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_getopt_arg_probe_main -c $< -o $@

$(BUILD)/libc_stdin_probe.o: tests/libc_stdin_probe.c include/cannedbsd/libc.h libc/include/stdio.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_stdin_probe_main -c $< -o $@

$(BUILD)/libc_file_probe.o: tests/libc_file_probe.c include/cannedbsd/libc.h libc/include/stdio.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_file_probe_main -c $< -o $@

$(BUILD)/libc_fread_probe.o: tests/libc_fread_probe.c include/cannedbsd/libc.h libc/include/stdio.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_fread_probe_main -c $< -o $@
$(BUILD)/libc_fwrite_probe.o: tests/libc_fwrite_probe.c include/cannedbsd/abi.h libc/include/stdio.h libc/include/errno.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Ilibc/include $(CFLAGS) -Dmain=cb_fwrite_probe_main -c tests/libc_fwrite_probe.c -o $@
$(BUILD)/libc_fwrite_wrapper_probe.o: tests/libc_fwrite_wrapper_probe.c include/cannedbsd/abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c tests/libc_fwrite_wrapper_probe.c -o $@
