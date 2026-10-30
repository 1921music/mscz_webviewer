# Wrapper around aria's render-core build.
#
# Its only job for now is to fix up build settings that aria's
# sandbox/engraving/CMakeLists.txt only gets right on iOS, Android and macOS,
# so that a Linux host build links. aria itself stays untouched.
#
# Configure with ARIA_DIR pointing at an aria checkout:
#   cmake -S . -B build/host -G Ninja -DARIA_DIR=/mnt/c/work/20260809/aria
#
# scripts/build-host.sh does this for you.

cmake_minimum_required(VERSION 3.26...3.31)

project(mscz_test LANGUAGES C CXX)

set(ARIA_DIR "" CACHE PATH "Path to an aria checkout.")
if(NOT ARIA_DIR OR NOT EXISTS "${ARIA_DIR}/sandbox/engraving/CMakeLists.txt")
    message(FATAL_ERROR "Set -DARIA_DIR to an aria checkout (got '${ARIA_DIR}').")
endif()

# engraving embeds 13 font families with Qt's qt_add_big_resources, whose
# generated .o files are not compiled -fPIC. A shared libengraving.so therefore
# fails to link with "relocation R_X86_64_PC32 against qt_resourceFeatureZstd".
# aria builds these static on iOS and Android for the same reason.
#
# BUILD_SHARED_LIBS alone does not do it: with Qt policy QTP0003 at OLD,
# qt_add_library() ignores BUILD_SHARED_LIBS and follows the flavour of the Qt
# build instead, and a distro Qt6 is a shared build. aria sets this policy only
# in its Android branch. Set it here so the Linux host build behaves the same.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
qt_policy(SET QTP0003 NEW)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory("${ARIA_DIR}/sandbox/engraving" aria-engraving)
