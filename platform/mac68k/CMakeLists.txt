cmake_minimum_required(VERSION 3.16)
project(cannedBSDMac C CXX ASM)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_compile_options(-Os -ffunction-sections -fdata-sections -Wall -Wextra)
include_directories(${ROOT}/include ${ROOT}/src)

# Keep the command/libc headers private to their original translation units.
add_library(cb_wc OBJECT ${ROOT}/commands/wc.c)
target_include_directories(cb_wc PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_wc PRIVATE main=cb_wc_main)
add_library(cb_yes OBJECT ${ROOT}/upstream/netbsd/usr.bin/yes/yes.c)
target_include_directories(cb_yes PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_yes PRIVATE main=cb_yes_main)
foreach(probe memory getopt truncate err terminal locale)
    add_library(cb_${probe}_probe OBJECT ${ROOT}/tests/libc_${probe}_probe.c)
    target_include_directories(cb_${probe}_probe PRIVATE ${ROOT}/libc/include)
endforeach()
target_compile_definitions(cb_locale_probe PRIVATE main=cb_locale_probe_main)
target_compile_definitions(cb_terminal_probe PRIVATE main=cb_terminal_probe_main)
target_compile_definitions(cb_err_probe PRIVATE main=cb_err_probe_main)
target_compile_definitions(cb_memory_probe PRIVATE main=cb_memory_probe_main)
target_compile_definitions(cb_getopt_probe PRIVATE main=cb_getoptprobe_main)
target_compile_definitions(cb_truncate_probe PRIVATE main=cb_truncate_probe_main)
add_library(cb_poll_probe OBJECT ${ROOT}/tests/libc_poll_source.c)
target_include_directories(cb_poll_probe PRIVATE ${ROOT}/libc/include)
add_library(cb_printenv OBJECT
    ${ROOT}/upstream/netbsd/usr.bin/printenv/printenv.c)
target_include_directories(cb_printenv PRIVATE ${ROOT}/libc/include)
target_compile_definitions(cb_printenv PRIVATE main=cb_printenv_main)
# See UPSTREAM.md's printenv entry: the pinned source's local
# `extern char **environ;` expands, once environ is cannedBSD's task-local
# accessor macro, into a declarator this toolchain's GCC also parses as an
# unprototyped redeclaration of cb_libc_environ_location.
target_compile_options(cb_printenv PRIVATE -Wno-strict-prototypes)
foreach(routine strlen strcmp memcpy memmove memcmp strchr)
    add_library(cb_${routine} OBJECT
        ${ROOT}/upstream/netbsd/common/lib/libc/string/${routine}.c)
    target_include_directories(cb_${routine} PRIVATE
        ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
endforeach()
target_compile_definitions(cb_strlen PRIVATE strlen=cb_libc_strlen)
target_compile_definitions(cb_strcmp PRIVATE CANNEDBSD_BUILDING_LIBC_STRCMP)
target_compile_definitions(cb_memcpy PRIVATE CANNEDBSD_BUILDING_LIBC_MEMCPY)
target_compile_definitions(cb_memmove PRIVATE CANNEDBSD_BUILDING_LIBC_MEMMOVE)
target_compile_definitions(cb_memcmp PRIVATE CANNEDBSD_BUILDING_LIBC_MEMCMP)

add_application(CannedBSD CREATOR "CnBD"
    main.c host_mac.c autorun.c root_dispatch.c context.S test_context.c application.r
    ${ROOT}/src/core.c ${ROOT}/src/executor.c ${ROOT}/src/programs.c
    ${ROOT}/src/ramfs.c ${ROOT}/src/shell.c ${ROOT}/src/vfs.c
    ${ROOT}/commands/wc_module.c ${ROOT}/commands/yes_module.c
    ${ROOT}/commands/printenv_module.c
    ${ROOT}/libc/cb_libc.c
    ${ROOT}/tests/libc_locale_probe_module.c
    ${ROOT}/tests/libc_terminal_probe_module.c
    ${ROOT}/tests/libc_err_probe_module.c
    ${ROOT}/tests/libc_poll_probe_module.c
    ${ROOT}/tests/libc_memory_probe_module.c
    ${ROOT}/tests/libc_getopt_probe_module.c
    ${ROOT}/tests/libc_truncate_probe_module.c)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_locale_probe> $<TARGET_OBJECTS:cb_terminal_probe> $<TARGET_OBJECTS:cb_poll_probe> $<TARGET_OBJECTS:cb_err_probe> $<TARGET_OBJECTS:cb_memory_probe>
    $<TARGET_OBJECTS:cb_getopt_probe> $<TARGET_OBJECTS:cb_truncate_probe>)
target_sources(CannedBSD PRIVATE
    $<TARGET_OBJECTS:cb_wc> $<TARGET_OBJECTS:cb_yes> $<TARGET_OBJECTS:cb_printenv>
    $<TARGET_OBJECTS:cb_strlen> $<TARGET_OBJECTS:cb_strcmp>
    $<TARGET_OBJECTS:cb_memcpy> $<TARGET_OBJECTS:cb_memmove>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_memcmp>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_strchr>)
# Lazy LoadSeg performs Toolbox work and must never run on a task stack.
# A single resident CODE segment keeps all runtime calls free of that trap.
target_link_options(CannedBSD PRIVATE -Wl,-gc-sections -Wl,--mac-strip-macsbug
    -Wl,--mac-single)
