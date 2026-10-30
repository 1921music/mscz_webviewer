# Builds the vendored MuseScore-derived engine plus this project's own code.
#
# Phase 1 still uses Qt, so its PNG/PDF output can serve as the reference the
# SVG provider gets compared against. MUSE_QT_SUPPORT goes OFF in Phase 2.
#
# Configure and build with scripts/build-host.sh.

cmake_minimum_required(VERSION 3.26...3.31)

project(mscz_test LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# --- the vendored engine ------------------------------------------------------

set(MU_ROOT ${CMAKE_CURRENT_LIST_DIR}/vendor/aria)
set(MUSE_FRAMEWORK_PATH ${MU_ROOT})
set(MUSE_FRAMEWORK_SRC_PATH ${MU_ROOT}/src/framework)
set(PROJECT_ROOT_DIR ${MU_ROOT})
set(THIRDPARTY_DIR ${MU_ROOT}/thirdparty)

list(PREPEND CMAKE_MODULE_PATH
    ${MU_ROOT}
    ${MU_ROOT}/buildscripts
    ${MU_ROOT}/buildscripts/cmake
    ${MU_ROOT}/src/framework/cmake
)

include(GetPaths)
include(${MU_ROOT}/src/framework/cmake/MuseDeclareOptions.cmake)

set(MUSE_QT_SUPPORT ON CACHE BOOL "" FORCE)
set(MUSE_ENABLE_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(MUSE_COMPILE_USE_PCH OFF CACHE BOOL "" FORCE)
set(MUSE_COMPILE_USE_UNITY OFF CACHE BOOL "" FORCE)

# Display only. Dropping playback also drops engraving's only dependency on the
# mpe module, which is why mpe is not vendored at all.
set(MUE_BUILD_ENGRAVING_PLAYBACK OFF CACHE BOOL "" FORCE)
set(ENGRAVING_NO_API ON CACHE BOOL "" FORCE)
set(MUE_BUILD_ENGRAVING_QML OFF CACHE BOOL "" FORCE)
set(MUE_BUILD_ENGRAVING_TESTS OFF CACHE BOOL "" FORCE)
set(MUE_BUILD_ENGRAVING_DEVTOOLS OFF CACHE BOOL "" FORCE)

# The bundled FreeType/HarfBuzz are not vendored; the host build uses the
# system ones and the wasm build will use emscripten ports.
set(MUE_COMPILE_USE_SYSTEM_FREETYPE ON CACHE BOOL "" FORCE)
set(MUE_COMPILE_USE_SYSTEM_HARFBUZZ ON CACHE BOOL "" FORCE)

# Fed to muse_framework_config.h by #cmakedefine. GlobalConfiguration's Linux
# branch does not compile without them, and aria's own build only sets them for
# Android. The engine embeds its resources, so the path they form is unused.
set(MUSE_APP_INSTALL_PREFIX "/usr/local" CACHE STRING "" FORCE)
set(MUSE_APP_INSTALL_NAME "mscz_test" CACHE STRING "" FORCE)

include(SetupBuildEnvironment)
include(GetPlatformInfo)
include(${MU_ROOT}/src/framework/cmake/MuseSetupConfiguration.cmake)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Svg Qml Quick Widgets Network)
qt_standard_project_setup()

# engraving embeds its fonts with qt_add_big_resources, whose generated .o files
# are not -fPIC, so a shared libengraving.so fails to link. BUILD_SHARED_LIBS
# alone does not do it: with QTP0003 at OLD, qt_add_library ignores it and
# follows the flavour of the Qt build, and a distro Qt6 is shared.
qt_policy(SET QTP0003 NEW)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(${MU_ROOT}/src/framework/global global)
add_subdirectory(${MU_ROOT}/src/framework/draw draw)
add_subdirectory(${MU_ROOT}/src/engraving engraving)

# --- our code -----------------------------------------------------------------

add_subdirectory(src)
