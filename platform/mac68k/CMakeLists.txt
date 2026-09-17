cmake_minimum_required(VERSION 3.16)
project(cannedBSDMac C CXX ASM)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
include(CheckCCompilerFlag)
check_c_compiler_flag("-fno-tree-loop-distribute-patterns" HAVE_NO_TREE_LOOP_DISTRIBUTE_PATTERNS)
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
# libc_file_probe.c reads MAXBSIZE from compat/netbsd/include/sys/param.h, so it
# needs the same include set the Linux build gives it in Makefile's
# $(BUILD)/libc_file_probe.o rule (-Icompat/netbsd/include -Ilibc/include).
# The two builds list include paths independently, so a probe that gains a
# compat-header dependency compiles on Linux and fails only here. See
# notes/iterations/MAC68K-INC-01.md.
target_include_directories(cb_file_probe PRIVATE ${ROOT}/compat/netbsd/include)
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
add_library(cb_strrchr OBJECT
    ${ROOT}/upstream/netbsd/common/lib/libc/string/strrchr.c)
target_include_directories(cb_strrchr PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
# GCC's loop-idiom recognition rewrites memset.c's fill loops into
# calls back to memset, resolving to cb_libc_memset via the link-name binding,
# causing unbounded self-recursion at runtime.
# -fno-tree-loop-distribute-patterns is the fix; added conditionally if supported.
add_library(cb_memset OBJECT
    ${ROOT}/upstream/netbsd/common/lib/libc/string/memset.c)
target_include_directories(cb_memset PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
target_compile_definitions(cb_memset PRIVATE CANNEDBSD_BUILDING_LIBC_MEMSET)
target_compile_options(cb_memset PRIVATE -fno-builtin-memset)
if(HAVE_NO_TREE_LOOP_DISTRIBUTE_PATTERNS)
    target_compile_options(cb_memset PRIVATE -fno-tree-loop-distribute-patterns)
endif()
add_library(cb_fts OBJECT ${ROOT}/libc/cb_fts.c)
target_include_directories(cb_fts PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)

# LS-02: pinned NetBSD ls.c/print.c/cmp.c/util.c, mirroring the Linux
# Makefile's include set (compat/netbsd/include for sys/param.h's MIN/
# MAX and sys/types.h, libc/include for the rest, upstream/netbsd/bin/ls
# for the files' own mutual "ls.h"/"extern.h" includes). Replaces the
# retired cannedBSD-owned commands/ls.c (LS-01).
#
# STATICS-RESET-01: ls/cat/mv/rm/cp's own file-scope statics need the same
# objcopy --redefine-sym/--globalize-symbol rename this ID's Linux Makefile
# rule applies -- see src/static_reset.c's own top comment for why a
# compile-time -D rename cannot do this job. CMAKE_OBJCOPY is CMake's own
# standard variable for whatever objcopy-equivalent the active toolchain
# provides; confirmed locally (pulled the real pinned
# ghcr.io/autc04/retro68 image .woodpecker/mac68k.yml uses and ran
# platform/mac68k/ci-build.sh directly, since it turned out pullable
# without needing Woodpecker's own role=retro68 backend) that
# m68k-apple-macos-objcopy exists and is real GNU objcopy 2.46 supporting
# both flags. The rename itself is applied as a single PRE_LINK step on
# the CannedBSD target below, not POST_BUILD on each of these OBJECT
# libraries -- also found by running the real build, not assumed: this
# CMake rejects PRE_BUILD/PRE_LINK/POST_BUILD on OBJECT library targets
# outright ("Target ... is an OBJECT library that may not have ..."),
# so the rename has to happen on the target that actually links, right
# before it does.
add_library(cb_ls OBJECT ${ROOT}/upstream/netbsd/bin/ls/ls.c)
target_include_directories(cb_ls PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include ${ROOT}/upstream/netbsd/bin/ls)
target_compile_definitions(cb_ls PRIVATE SMALL ls_main=cb_ls_main)

add_library(cb_ls_print OBJECT ${ROOT}/upstream/netbsd/bin/ls/print.c)
target_include_directories(cb_ls_print PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include ${ROOT}/upstream/netbsd/bin/ls)
target_compile_definitions(cb_ls_print PRIVATE SMALL)

add_library(cb_ls_cmp OBJECT ${ROOT}/upstream/netbsd/bin/ls/cmp.c)
target_include_directories(cb_ls_cmp PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include ${ROOT}/upstream/netbsd/bin/ls)

add_library(cb_ls_util OBJECT ${ROOT}/upstream/netbsd/bin/ls/util.c)
target_include_directories(cb_ls_util PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include ${ROOT}/upstream/netbsd/bin/ls)

add_library(cb_rm OBJECT ${ROOT}/upstream/netbsd/bin/rm/rm.c)
target_include_directories(cb_rm PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
target_compile_definitions(cb_rm PRIVATE main=cb_rm_main)

add_library(cb_mv OBJECT ${ROOT}/upstream/netbsd/bin/mv/mv.c)
target_include_directories(cb_mv PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include ${ROOT}/upstream/netbsd/bin/mv)
target_compile_definitions(cb_mv PRIVATE main=cb_mv_main)

add_library(cb_cat OBJECT ${ROOT}/upstream/netbsd/bin/cat/cat.c)
target_include_directories(cb_cat PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include)
target_compile_definitions(cb_cat PRIVATE main=cb_cat_main)

add_library(cb_cp OBJECT ${ROOT}/upstream/netbsd/bin/cp/cp.c)
target_include_directories(cb_cp PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include ${ROOT}/upstream/netbsd/bin/cp)
target_compile_definitions(cb_cp PRIVATE SMALL main=cb_cp_main)

add_library(cb_cp_utils OBJECT ${ROOT}/upstream/netbsd/bin/cp/utils.c)
target_include_directories(cb_cp_utils PRIVATE
    ${ROOT}/compat/netbsd/include ${ROOT}/libc/include ${ROOT}/upstream/netbsd/bin/cp)
target_compile_definitions(cb_cp_utils PRIVATE SMALL)

add_application(CannedBSD CREATOR "CnBD"
    main.c host_mac.c autorun.c root_dispatch.c context.S test_context.c application.r
    ${ROOT}/src/core.c ${ROOT}/src/terminal.c ${ROOT}/src/executor.c ${ROOT}/src/programs.c
    ${ROOT}/src/static_reset.c
    ${ROOT}/src/ramfs.c ${ROOT}/src/shell.c ${ROOT}/src/vfs.c
    ${ROOT}/commands/dirname_module.c ${ROOT}/commands/basename_module.c
    ${ROOT}/commands/wc_module.c ${ROOT}/commands/yes_module.c
    ${ROOT}/commands/printenv_module.c ${ROOT}/commands/echo_module.c
    ${ROOT}/commands/head_module.c
    ${ROOT}/commands/ls_module.c
    ${ROOT}/commands/rm_module.c
    ${ROOT}/commands/mv_module.c
    ${ROOT}/commands/cat_module.c
    ${ROOT}/commands/cp_module.c
    ${ROOT}/tests/terminal_engine_probe.c
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
# STATICS-RESET-01: applied once, right before CannedBSD's own link step
# consumes these OBJECT libraries' output below -- see cb_ls's own build
# rule comment above for why this cannot be a POST_BUILD step on each
# OBJECT library instead.
add_custom_command(TARGET CannedBSD PRE_LINK
    COMMAND ${CMAKE_OBJCOPY} --redefine-sym output=cb_ls_output
        --globalize-symbol=cb_ls_output $<TARGET_OBJECTS:cb_ls>
    COMMAND ${CMAKE_OBJCOPY}
        --redefine-sym dflag=cb_rm_dflag --redefine-sym eval=cb_rm_eval
        --redefine-sym fflag=cb_rm_fflag --redefine-sym iflag=cb_rm_iflag
        --redefine-sym Pflag=cb_rm_Pflag --redefine-sym vflag=cb_rm_vflag
        --redefine-sym Wflag=cb_rm_Wflag --redefine-sym xflag=cb_rm_xflag
        --globalize-symbol=cb_rm_dflag --globalize-symbol=cb_rm_eval
        --globalize-symbol=cb_rm_fflag --globalize-symbol=cb_rm_iflag
        --globalize-symbol=cb_rm_Pflag --globalize-symbol=cb_rm_vflag
        --globalize-symbol=cb_rm_Wflag --globalize-symbol=cb_rm_xflag
        $<TARGET_OBJECTS:cb_rm>
    COMMAND ${CMAKE_OBJCOPY}
        --redefine-sym fflg=cb_mv_fflg --redefine-sym hflg=cb_mv_hflg
        --redefine-sym iflg=cb_mv_iflg --redefine-sym vflg=cb_mv_vflg
        --globalize-symbol=cb_mv_fflg --globalize-symbol=cb_mv_hflg
        --globalize-symbol=cb_mv_iflg --globalize-symbol=cb_mv_vflg
        $<TARGET_OBJECTS:cb_mv>
    COMMAND ${CMAKE_OBJCOPY}
        --redefine-sym bflag=cb_cat_bflag --redefine-sym eflag=cb_cat_eflag
        --redefine-sym fflag=cb_cat_fflag --redefine-sym lflag=cb_cat_lflag
        --redefine-sym nflag=cb_cat_nflag --redefine-sym sflag=cb_cat_sflag
        --redefine-sym tflag=cb_cat_tflag --redefine-sym vflag=cb_cat_vflag
        --redefine-sym rval=cb_cat_rval
        --globalize-symbol=cb_cat_bflag --globalize-symbol=cb_cat_eflag
        --globalize-symbol=cb_cat_fflag --globalize-symbol=cb_cat_lflag
        --globalize-symbol=cb_cat_nflag --globalize-symbol=cb_cat_sflag
        --globalize-symbol=cb_cat_tflag --globalize-symbol=cb_cat_vflag
        --globalize-symbol=cb_cat_rval
        $<TARGET_OBJECTS:cb_cat>
    COMMAND ${CMAKE_OBJCOPY} --redefine-sym dnesp=cb_cp_dnesp
        --globalize-symbol=cb_cp_dnesp $<TARGET_OBJECTS:cb_cp>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_progname_probe> $<TARGET_OBJECTS:cb_locale_probe> $<TARGET_OBJECTS:cb_terminal_probe> $<TARGET_OBJECTS:cb_poll_probe> $<TARGET_OBJECTS:cb_err_probe> $<TARGET_OBJECTS:cb_warn_probe> $<TARGET_OBJECTS:cb_strcpy_probe> $<TARGET_OBJECTS:cb_memory_probe>
    $<TARGET_OBJECTS:cb_fwrite_probe> $<TARGET_OBJECTS:cb_fread_probe> $<TARGET_OBJECTS:cb_file_probe> $<TARGET_OBJECTS:cb_stdin_probe> $<TARGET_OBJECTS:cb_getopt_arg_probe> $<TARGET_OBJECTS:cb_getopt_probe> $<TARGET_OBJECTS:cb_truncate_probe> $<TARGET_OBJECTS:cb_dirname_probe> $<TARGET_OBJECTS:cb_dirent_probe> $<TARGET_OBJECTS:cb_exec_errno_probe> $<TARGET_OBJECTS:cb_basename_probe> $<TARGET_OBJECTS:cb_argv_probe> $<TARGET_OBJECTS:cb_stdio_state_probe> $<TARGET_OBJECTS:cb_stdio_oldtable_probe> $<TARGET_OBJECTS:cb_strtoimax_probe>)
target_sources(CannedBSD PRIVATE
    $<TARGET_OBJECTS:cb_dirname_command> $<TARGET_OBJECTS:cb_basename_command> $<TARGET_OBJECTS:cb_wc> $<TARGET_OBJECTS:cb_yes> $<TARGET_OBJECTS:cb_printenv> $<TARGET_OBJECTS:cb_echo> $<TARGET_OBJECTS:cb_head>
    $<TARGET_OBJECTS:cb_ls> $<TARGET_OBJECTS:cb_ls_print> $<TARGET_OBJECTS:cb_ls_cmp> $<TARGET_OBJECTS:cb_ls_util> $<TARGET_OBJECTS:cb_rm> $<TARGET_OBJECTS:cb_mv> $<TARGET_OBJECTS:cb_cat> $<TARGET_OBJECTS:cb_cp> $<TARGET_OBJECTS:cb_cp_utils>
    $<TARGET_OBJECTS:cb_strlen> $<TARGET_OBJECTS:cb_strcmp> $<TARGET_OBJECTS:cb_strcpy>
    $<TARGET_OBJECTS:cb_memcpy> $<TARGET_OBJECTS:cb_memmove>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_memcmp>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_strchr>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_dirname>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_basename>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_strtoimax>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_strrchr>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_memset>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_fts>)
# Lazy LoadSeg performs Toolbox work and must never run on a task stack.
# A single resident CODE segment keeps all runtime calls free of that trap.
target_link_options(CannedBSD PRIVATE -Wl,-gc-sections -Wl,--mac-strip-macsbug
    -Wl,--mac-single)
