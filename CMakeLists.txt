# Builds the vendored MuseScore-derived engine plus this project's own code.
#
# Phase 1 still uses Qt, so its PNG/PDF output can serve as the reference the
# SVG provider gets compared against. MUSE_QT_SUPPORT goes OFF in Phase 2.
#
# Configure and build with scripts/build-host.sh.

cmake_minimum_required(VERSION 3.26...3.31)

project(mscz_test LANGUAGES C CXX)

set(MSCZ_PLAYBACK "EAS" CACHE STRING "GM playback backend: OFF, EAS, TSF_SF3")
set_property(CACHE MSCZ_PLAYBACK PROPERTY STRINGS OFF EAS TSF_SF3)
if(NOT MSCZ_PLAYBACK MATCHES "^(OFF|EAS|TSF_SF3)$")
    message(FATAL_ERROR "MSCZ_PLAYBACK must be OFF, EAS or TSF_SF3")
endif()

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

# The two levers that change how long a build takes rather than what it
# produces. Our own options, forced onto the upstream ones: upstream's
# MuseDeclareOptions.cmake has already created those cache entries by now --
# with PCH on -- so a plain set() of them would be a no-op and the default
# would silently become upstream's. scripts/bench-build.sh measures these.
#
# PCH is off because it cannot work here: MuseCreateModule reuses a muse_pch
# target that lives in aria's buildscripts/pch, which we do not vendor.
#
# Unity is on: measured, it turns 470 translation units into 121 and a full
# wasm build from clean from 435 seconds into 204. The output is the same --
# both demo scores still match the native build's layout exactly and page 1 is
# byte-identical. It also makes the .wasm 399KB smaller raw, though only 6.5KB
# smaller brotli, so treat that as a build-time win rather than a size one.
#
# Turn it off if a newly added file collides with another in its module; that
# is the known hazard of unity builds, and scripts/bench-build.sh can measure
# what it costs to do without.
option(MSCZ_USE_PCH "Precompile the module headers (needs a muse_pch target)" OFF)
option(MSCZ_USE_UNITY "Compile each module as a few large translation units" ON)
set(MUSE_COMPILE_USE_PCH ${MSCZ_USE_PCH} CACHE BOOL "" FORCE)
set(MUSE_COMPILE_USE_UNITY ${MSCZ_USE_UNITY} CACHE BOOL "" FORCE)

# ccache if it is there. Worth having: with the cache warm the same full wasm
# build takes 88 seconds instead of 204, which is what makes flipping between
# configurations -- the whole of a size investigation -- bearable.
find_program(MSCZ_CCACHE ccache)
if (MSCZ_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${MSCZ_CCACHE}" CACHE STRING "")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${MSCZ_CCACHE}" CACHE STRING "")
    message(STATUS "mscz: compiling through ${MSCZ_CCACHE}")
endif()

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

# Keep the desktop playback/MPE stack disabled. Optional GM playback uses the
# compatibility MIDI renderer and our independent small synth module instead.
set(MUE_BUILD_ENGRAVING_PLAYBACK OFF CACHE BOOL "" FORCE)
set(ENGRAVING_NO_API ON CACHE BOOL "" FORCE)
set(MUE_BUILD_ENGRAVING_QML OFF CACHE BOOL "" FORCE)
set(MUE_BUILD_ENGRAVING_TESTS OFF CACHE BOOL "" FORCE)
set(MUE_BUILD_ENGRAVING_DEVTOOLS OFF CACHE BOOL "" FORCE)

# FreeType and HarfBuzz. The host build can use the distro's packages; the
# wasm build must not, because emscripten ports fetch tarballs and because we
# want to pick FreeType's module set. See cmake/MsczThirdparty.cmake.
if (EMSCRIPTEN)
    set(MSCZ_VENDORED_THIRDPARTY ON)
else()
    set(MSCZ_VENDORED_THIRDPARTY OFF CACHE BOOL "Build FreeType/HarfBuzz from vendor/thirdparty")
endif()

if (MSCZ_VENDORED_THIRDPARTY)
    set(MUE_COMPILE_USE_SYSTEM_FREETYPE OFF CACHE BOOL "" FORCE)
    set(MUE_COMPILE_USE_SYSTEM_HARFBUZZ OFF CACHE BOOL "" FORCE)
else()
    set(MUE_COMPILE_USE_SYSTEM_FREETYPE ON CACHE BOOL "" FORCE)
    set(MUE_COMPILE_USE_SYSTEM_HARFBUZZ ON CACHE BOOL "" FORCE)
endif()

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

# There is no user interface here, so no one ever reads a TranslatableString.
# They are not free, though: engraving's conversion tables carry two per entry
# and each one converts UTF-8 to UTF-16 and allocates before main. On by
# default for every configuration, so the native and wasm builds stay
# comparable byte for byte.
option(MSCZ_NO_TRANSLATABLE_STRINGS "Do not build the translatable string tables" ON)
if (MSCZ_NO_TRANSLATABLE_STRINGS)
    add_compile_definitions(MUSE_NO_TRANSLATABLE_STRINGS)
endif()

if (EMSCRIPTEN)
    # Size over speed, and LTO at compile time as well as at link time -- the
    # cross-translation-unit pruning is where most of the saving is, and it
    # cannot happen if the objects do not carry bitcode.
    #
    # But only when the build is for size. Measured with .ninja_log, 98% of a
    # full build is compiling and 2% is linking, so the way to a quick build is
    # to compile cheaply -- and a run of check-wasm.sh does not care how big
    # the binary is. CMAKE_BUILD_TYPE=Debug gives a correctness build; use
    # MinSizeRel for anything whose size gets quoted.
    # LTO is separable from -Oz because it is what makes a size *attribution*
    # impossible: it merges functions across translation units, so the linker
    # can only say which merged body a byte is in, not which source file it
    # came from. Turning it off -- together with the unity build -- gives one
    # object per source file and a linker map that attributes every output byte
    # exactly. The totals are then a little larger than the shipping build's,
    # but the proportions are what a size budget needs.
    # See scripts/wasm-size/breakdown.sh.
    option(MSCZ_WASM_LTO "Link the wasm with LTO (off to get an attributable map)" ON)

    if (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel" OR CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-Oz)
        if (MSCZ_WASM_LTO)
            add_compile_options(-flto)
        endif()
        set(MSCZ_WASM_SIZE_BUILD ON)
    else()
        message(STATUS "mscz: a quick wasm build -- correct, but not small")
    endif()
endif()

if (MSCZ_VENDORED_THIRDPARTY)
    include(cmake/MsczThirdparty.cmake)
endif()

add_subdirectory(${MU_ROOT}/src/framework/global global)
add_subdirectory(${MU_ROOT}/src/framework/draw draw)
add_subdirectory(${MU_ROOT}/src/engraving engraving)

# --- our code -----------------------------------------------------------------

add_subdirectory(src)
