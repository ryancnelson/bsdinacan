cmake_minimum_required(VERSION 3.16)
project(cannedBSDMac C CXX ASM)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_compile_options(-Os -ffunction-sections -fdata-sections -Wall -Wextra)
include_directories(${ROOT}/include ${ROOT}/src)

# Keep the command/libc headers private to their original translation units.
add_library(cb_dirname_command OBJECT ${ROOT}/upstream/netbsd/usr.bin/dirname/dirname.c)
target_include_directories(cb_dirname_command PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_dirname_command PRIVATE main=cb_dirname_main)
add_library(cb_basename_command OBJECT ${ROOT}/upstream/netbsd/usr.bin/basename/basename.c)
target_include_directories(cb_basename_command PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_basename_command PRIVATE main=cb_basename_main)
add_library(cb_wc OBJECT ${ROOT}/commands/wc.c)
target_include_directories(cb_wc PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_wc PRIVATE main=cb_wc_main)
add_library(cb_yes OBJECT ${ROOT}/upstream/netbsd/usr.bin/yes/yes.c)
target_include_directories(cb_yes PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_yes PRIVATE main=cb_yes_main)
foreach(probe fwrite fread file stdin memory getopt truncate err warn strcpy terminal locale dirname dirent progname exec_errno basename stdio_state stdio_oldtable argv getopt_arg strtoimax)
    add_library(cb_${probe}_probe OBJECT ${ROOT}/tests/libc_${probe}_probe.c)
    target_include_directories(cb_${probe}_probe PRIVATE ${ROOT}/libc/include)
endforeach()
target_compile_definitions(cb_fread_probe PRIVATE main=cb_fread_probe_main)
target_compile_definitions(cb_file_probe PRIVATE main=cb_file_probe_main)
target_compile_definitions(cb_stdin_probe PRIVATE main=cb_stdin_probe_main)
target_compile_definitions(cb_progname_probe PRIVATE main=cb_progname_probe_main)
target_compile_definitions(cb_locale_probe PRIVATE main=cb_locale_probe_main)
target_compile_definitions(cb_terminal_probe PRIVATE main=cb_terminal_probe_main)
target_compile_definitions(cb_err_probe PRIVATE main=cb_err_probe_main)
target_compile_definitions(cb_warn_probe PRIVATE main=cb_warn_probe_main)
target_compile_definitions(cb_strcpy_probe PRIVATE main=cb_strcpy_probe_main)
target_compile_definitions(cb_memory_probe PRIVATE main=cb_memory_probe_main)
target_compile_definitions(cb_getopt_arg_probe PRIVATE main=cb_getopt_arg_probe_main)
target_compile_definitions(cb_getopt_probe PRIVATE main=cb_getoptprobe_main)
target_compile_definitions(cb_truncate_probe PRIVATE main=cb_truncate_probe_main)
target_compile_definitions(cb_dirname_probe PRIVATE main=cb_dirname_probe_main)
target_compile_definitions(cb_dirent_probe PRIVATE main=cb_direntprobe_main)
target_compile_definitions(cb_argv_probe PRIVATE main=cb_argv_probe_main)
target_compile_definitions(cb_stdio_state_probe PRIVATE main=cb_stdio_state_probe_main)
target_compile_definitions(cb_fwrite_probe PRIVATE main=cb_fwrite_probe_main)
target_compile_definitions(cb_stdio_oldtable_probe PRIVATE main=cb_stdio_oldtable_main)
target_compile_definitions(cb_exec_errno_probe PRIVATE main=cb_exec_errno_probe_main)
target_compile_definitions(cb_basename_probe PRIVATE main=cb_basename_probe_main)
target_compile_definitions(cb_strtoimax_probe PRIVATE main=cb_strtoimax_probe_main)
add_library(cb_poll_probe OBJECT ${ROOT}/tests/libc_poll_source.c)
target_include_directories(cb_poll_probe PRIVATE ${ROOT}/libc/include)
add_library(cb_head OBJECT ${ROOT}/upstream/netbsd/usr.bin/head/head.c)
target_include_directories(cb_head PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_head PRIVATE main=cb_head_main)
target_compile_options(cb_head PRIVATE -fstack-usage)
add_library(cb_echo OBJECT ${ROOT}/upstream/netbsd/bin/echo/echo.c)
target_include_directories(cb_echo PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_echo PRIVATE main=cb_netbsdecho_main)
# See UPSTREAM.md's echo entry: the pinned source's main is marked
# /* ARGSUSED */ (lint-only) because it never reads argc.
target_compile_options(cb_echo PRIVATE -Wno-unused-parameter)
add_library(cb_printenv OBJECT
    ${ROOT}/upstream/netbsd/usr.bin/printenv/printenv.c)
target_include_directories(cb_printenv PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_printenv PRIVATE main=cb_printenv_main)
# See UPSTREAM.md's printenv entry: the pinned source's local
# `extern char **environ;` expands, once environ is cannedBSD's task-local
# accessor macro, into a declarator this toolchain's GCC also parses as an
# unprototyped redeclaration of cb_libc_environ_location.
target_compile_options(cb_printenv PRIVATE -Wno-strict-prototypes)
foreach(routine strlen strcmp strcpy memcpy memmove memcmp strchr)
    add_library(cb_${routine} OBJECT
        ${ROOT}/upstream/netbsd/common/lib/libc/string/${routine}.c)
    target_include_directories(cb_${routine} PRIVATE
        ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
endforeach()
target_compile_definitions(cb_strlen PRIVATE strlen=cb_libc_strlen)
target_compile_definitions(cb_strcmp PRIVATE CANNEDBSD_BUILDING_LIBC_STRCMP)
target_compile_definitions(cb_strcpy PRIVATE CANNEDBSD_BUILDING_LIBC_STRCPY)
target_compile_definitions(cb_memcpy PRIVATE CANNEDBSD_BUILDING_LIBC_MEMCPY)
target_compile_definitions(cb_memmove PRIVATE CANNEDBSD_BUILDING_LIBC_MEMMOVE)
target_compile_definitions(cb_memcmp PRIVATE CANNEDBSD_BUILDING_LIBC_MEMCMP)
add_library(cb_dirname OBJECT ${ROOT}/upstream/netbsd/lib/libc/gen/dirname.c)
target_include_directories(cb_dirname PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
target_compile_definitions(cb_dirname PRIVATE dirname=cb_libc_dirname_upstream)
add_library(cb_basename OBJECT ${ROOT}/upstream/netbsd/lib/libc/gen/basename.c)
target_include_directories(cb_basename PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
target_compile_definitions(cb_basename PRIVATE basename=cb_libc_basename_upstream)
add_library(cb_strtoimax OBJECT
    ${ROOT}/upstream/netbsd/common/lib/libc/stdlib/strtoimax.c)
target_include_directories(cb_strtoimax PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
target_compile_definitions(cb_strtoimax PRIVATE
    HAVE_NBTOOL_CONFIG_H=1 strtoimax=cb_libc_strtoimax)

add_application(CannedBSD CREATOR "CnBD"
    main.c host_mac.c autorun.c root_dispatch.c context.S test_context.c application.r
    ${ROOT}/src/core.c ${ROOT}/src/executor.c ${ROOT}/src/programs.c
    ${ROOT}/src/ramfs.c ${ROOT}/src/shell.c ${ROOT}/src/vfs.c
    ${ROOT}/commands/dirname_module.c ${ROOT}/commands/basename_module.c
    ${ROOT}/commands/wc_module.c ${ROOT}/commands/yes_module.c
    ${ROOT}/commands/printenv_module.c ${ROOT}/commands/echo_module.c
    ${ROOT}/commands/head_module.c
    ${ROOT}/tests/signal_probe.c
    ${ROOT}/tests/tee_state_probe.c
    ${ROOT}/tests/console_write_probe.c
    ${ROOT}/tests/head_probe.c
    ${ROOT}/libc/cb_libc.c
    ${ROOT}/tests/libc_locale_probe_module.c
    ${ROOT}/tests/libc_terminal_probe_module.c
    ${ROOT}/tests/libc_err_probe_module.c
    ${ROOT}/tests/libc_warn_probe_module.c
    ${ROOT}/tests/libc_strcpy_probe_module.c
    ${ROOT}/tests/libc_poll_probe_module.c
    ${ROOT}/tests/libc_memory_probe_module.c
    ${ROOT}/tests/libc_argv_probe_module.c
    ${ROOT}/tests/libc_fread_probe_module.c
    ${ROOT}/tests/libc_fread_compat_module.c
    ${ROOT}/tests/libc_file_probe_module.c
    ${ROOT}/tests/libc_file_compat_module.c
    ${ROOT}/tests/libc_stdin_probe_module.c
    ${ROOT}/tests/libc_stdin_compat_module.c
    ${ROOT}/tests/libc_stdio_state_probe_module.c
    ${ROOT}/tests/libc_fwrite_wrapper_probe.c
    ${ROOT}/tests/libc_fwrite_compat_module.c
    ${ROOT}/tests/libc_fwrite_probe_module.c
    ${ROOT}/tests/libc_stdio_oldtable_probe_module.c
    ${ROOT}/tests/libc_getopt_arg_probe_module.c
    ${ROOT}/tests/libc_getopt_probe_module.c
    ${ROOT}/tests/libc_truncate_probe_module.c
    ${ROOT}/tests/libc_dirname_probe_module.c
    ${ROOT}/tests/libc_dirent_probe_module.c
    ${ROOT}/tests/libc_progname_probe_module.c
    ${ROOT}/tests/libc_basename_probe_module.c
    ${ROOT}/tests/libc_strtoimax_probe_module.c
    ${ROOT}/tests/vfs_executable_probe.c)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_progname_probe> $<TARGET_OBJECTS:cb_locale_probe> $<TARGET_OBJECTS:cb_terminal_probe> $<TARGET_OBJECTS:cb_poll_probe> $<TARGET_OBJECTS:cb_err_probe> $<TARGET_OBJECTS:cb_warn_probe> $<TARGET_OBJECTS:cb_strcpy_probe> $<TARGET_OBJECTS:cb_memory_probe>
    $<TARGET_OBJECTS:cb_fwrite_probe> $<TARGET_OBJECTS:cb_fread_probe> $<TARGET_OBJECTS:cb_file_probe> $<TARGET_OBJECTS:cb_stdin_probe> $<TARGET_OBJECTS:cb_getopt_arg_probe> $<TARGET_OBJECTS:cb_getopt_probe> $<TARGET_OBJECTS:cb_truncate_probe> $<TARGET_OBJECTS:cb_dirname_probe> $<TARGET_OBJECTS:cb_dirent_probe> $<TARGET_OBJECTS:cb_exec_errno_probe> $<TARGET_OBJECTS:cb_basename_probe> $<TARGET_OBJECTS:cb_argv_probe> $<TARGET_OBJECTS:cb_stdio_state_probe> $<TARGET_OBJECTS:cb_stdio_oldtable_probe> $<TARGET_OBJECTS:cb_strtoimax_probe>)
target_sources(CannedBSD PRIVATE
    $<TARGET_OBJECTS:cb_dirname_command> $<TARGET_OBJECTS:cb_basename_command> $<TARGET_OBJECTS:cb_wc> $<TARGET_OBJECTS:cb_yes> $<TARGET_OBJECTS:cb_printenv> $<TARGET_OBJECTS:cb_echo> $<TARGET_OBJECTS:cb_head>
    $<TARGET_OBJECTS:cb_strlen> $<TARGET_OBJECTS:cb_strcmp> $<TARGET_OBJECTS:cb_strcpy>
    $<TARGET_OBJECTS:cb_memcpy> $<TARGET_OBJECTS:cb_memmove>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_memcmp>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_strchr>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_dirname>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_basename>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_strtoimax>)
# Lazy LoadSeg performs Toolbox work and must never run on a task stack.
# A single resident CODE segment keeps all runtime calls free of that trap.
target_link_options(CannedBSD PRIVATE -Wl,-gc-sections -Wl,--mac-strip-macsbug
    -Wl,--mac-single)
