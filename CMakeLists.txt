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

# Deliberately not FORCE: Phase 2 turns this off from the command line, and a
# forced cache entry would silently win over -DMUSE_QT_SUPPORT=OFF. Everything
# below that we never want overridden does use FORCE.
set(MUSE_QT_SUPPORT ON CACHE BOOL "Build the engine against Qt")

set(MUSE_ENABLE_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(MUSE_COMPILE_USE_PCH OFF CACHE BOOL "" FORCE)
set(MUSE_COMPILE_USE_UNITY OFF CACHE BOOL "" FORCE)

# Accessibility is a screen-reader bridge whose interfaces are Qt-typed
# (QString, QRect, QVariant in the signatures), so it cannot come along into
# the Qt-free build. Upstream already has the switch and 21 files guard
# themselves with it.
if (NOT MUSE_QT_SUPPORT)
    set(ENGRAVING_NO_ACCESSIBILITY ON CACHE BOOL "" FORCE)
endif()

# MessageBox asks IInteractive to put a dialog up, and IInteractive's interface
# is QUrl-typed. A renderer has nobody to ask, so the engine's own switch for
# "no way to interact" also comes on with Qt off.
if (NOT MUSE_QT_SUPPORT)
    add_compile_definitions(ENGRAVING_NO_INTERACTIVE)
endif()

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

if (NOT MUSE_QT_SUPPORT)
    # FontsDatabase registers fonts with QFontDatabase and QFont substitutions
    # under this one; with Qt gone there is nothing to register them with.
    set(MUSE_MODULE_DRAW_USE_QTFONTMETRICS OFF CACHE BOOL "" FORCE)

    # Settings takes a cross-process lock through IMultiWindowsProvider, whose
    # interface is QStringList-typed. One renderer, one process, no windows.
    set(MUSE_MODULE_MULTIWINDOWS OFF CACHE BOOL "" FORCE)

    # Interactive puts dialogs up through IInteractiveProvider, which is
    # QVariantMap-typed. There is no user to ask.
    set(MUSE_MODULE_UI OFF CACHE BOOL "" FORCE)
endif()

include(SetupBuildEnvironment)
include(GetPlatformInfo)
include(${MU_ROOT}/src/framework/cmake/MuseSetupConfiguration.cmake)

if (MUSE_QT_SUPPORT)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Svg Qml Quick Widgets Network)
    qt_standard_project_setup()

    # engraving embeds its fonts with qt_add_big_resources, whose generated .o
    # files are not -fPIC, so a shared libengraving.so fails to link.
    # BUILD_SHARED_LIBS alone does not do it: with QTP0003 at OLD,
    # qt_add_library ignores it and follows the flavour of the Qt build, and a
    # distro Qt6 is shared.
    qt_policy(SET QTP0003 NEW)
endif()

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(${MU_ROOT}/src/framework/global global)
add_subdirectory(${MU_ROOT}/src/framework/draw draw)
add_subdirectory(${MU_ROOT}/src/engraving engraving)

# --- our code -----------------------------------------------------------------

add_subdirectory(src)
