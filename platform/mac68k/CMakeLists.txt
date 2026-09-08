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
    ${ROOT}/libc/cb_libc.c)
target_sources(CannedBSD PRIVATE
    $<TARGET_OBJECTS:cb_wc> $<TARGET_OBJECTS:cb_yes>
    $<TARGET_OBJECTS:cb_strlen> $<TARGET_OBJECTS:cb_strcmp>
    $<TARGET_OBJECTS:cb_memcpy> $<TARGET_OBJECTS:cb_memmove>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_memcmp>)
target_sources(CannedBSD PRIVATE $<TARGET_OBJECTS:cb_strchr>)
# Lazy LoadSeg performs Toolbox work and must never run on a task stack.
# A single resident CODE segment keeps all runtime calls free of that trap.
target_link_options(CannedBSD PRIVATE -Wl,-gc-sections -Wl,--mac-strip-macsbug
    -Wl,--mac-single)
