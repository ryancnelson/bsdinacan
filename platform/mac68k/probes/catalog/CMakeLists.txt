cmake_minimum_required(VERSION 3.16)
project(MoreFilesCatalogProbe C CXX)
if(NOT PROBE_COMMIT MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "PROBE_COMMIT must identify the tested source")
endif()
# Dedicated build/volume named for the existing Finder launcher only.
# Artifact publication identifies this as MoreFilesProbe, never ordinary cannedBSD.
add_application(CannedBSD CREATOR "RUSE" main.c catalog.c ../../application.r)
target_include_directories(CannedBSD PRIVATE "${FIXTURE_INCLUDE}")
target_compile_definitions(CannedBSD PRIVATE PROBE_COMMIT="${PROBE_COMMIT}")
target_compile_options(CannedBSD PRIVATE -Os -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
target_link_options(CannedBSD PRIVATE -Wl,-gc-sections -Wl,--mac-strip-macsbug -Wl,--mac-single)
