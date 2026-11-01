# FreeType, HarfBuzz and zlib, built from vendor/thirdparty.
#
# The host build can use the distro's packages, which is quicker and is what
# Phase 1 and 2 did. The wasm build cannot: emscripten ports fetch tarballs,
# and where TLS is intercepted a large HTTPS download can come back truncated.
# Picking FreeType's module set is reason enough on its own -- a port would not
# let us. So MSCZ_VENDORED_THIRDPARTY defaults to the emscripten case and can
# be turned on for the host to compare the two.
#
# Defines, to match what aria's SetupFreeType/SetupHarfBuzz produce:
#   FREETYPE_LIBRARIES / FREETYPE_INCLUDE_DIRS
#   HARFBUZZ_LIBRARIES / HARFBUZZ_INCLUDE_DIRS
# and mscz_zlib, which global links instead of the emscripten zlib port.

set(MSCZ_THIRDPARTY ${CMAKE_CURRENT_LIST_DIR}/../vendor/thirdparty)
cmake_path(NORMAL_PATH MSCZ_THIRDPARTY)

# --- FreeType -----------------------------------------------------------------

# Everything optional off: we open OTF/CFF and TrueType from memory, read
# outlines and metrics, and never rasterise, so there is nothing for zlib,
# bzip2, libpng or brotli to do. HarfBuzz is disabled here as a *FreeType*
# dependency (it is only used to improve autohinting) -- the shaping HarfBuzz
# is a separate target below and does not create a cycle.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

add_subdirectory(${MSCZ_THIRDPARTY}/freetype freetype EXCLUDE_FROM_ALL)

# The driver sources all compile; what decides which ones survive the link is
# the table ftinit.c builds, and that comes from this header. Measured at
# 140 KB brotli against 209 KB for the full set -- see scripts/wasm-size.
set(MSCZ_FTMODULE ${CMAKE_CURRENT_LIST_DIR}/../scripts/wasm-size/ftmodule-min.h)
cmake_path(NORMAL_PATH MSCZ_FTMODULE)

target_compile_definitions(freetype PRIVATE
    "FT_CONFIG_MODULES_H=\"${MSCZ_FTMODULE}\""
)

# And the features inside the drivers we keep: no hinting, since hinting is for
# rasterising and we only read outlines, and no compressed containers. In the
# binary before this, TT_RunIns -- the TrueType bytecode interpreter -- was
# 13.6 KB and FT_Gzip_Uncompress 8.8 KB, for things we never do.
#
# PUBLIC, unlike the module list: the module list only reaches ftinit.c, but
# the options header is included by FreeType's public headers, so everything
# that includes them has to agree with how the library was built.
if (MSCZ_TRIM_FREETYPE_OPTIONS)
    set(MSCZ_FTOPTION ${CMAKE_CURRENT_LIST_DIR}/../scripts/wasm-size/ftoption-min.h)
    cmake_path(NORMAL_PATH MSCZ_FTOPTION)
    target_compile_definitions(freetype PUBLIC
        "FT_CONFIG_OPTIONS_H=\"${MSCZ_FTOPTION}\""
    )
endif()

set(FREETYPE_LIBRARIES freetype)
set(FREETYPE_INCLUDE_DIRS ${MSCZ_THIRDPARTY}/freetype/include)

# --- HarfBuzz -----------------------------------------------------------------

# Built from the single-file amalgam rather than the 142 sources: one
# translation unit inlines across what would otherwise be separate objects,
# which is most of why HB_TINY lands at 76 KB brotli.
#
# HB_TINY keeps the OpenType shaper -- the part MuseScore uses through
# hb_ft_font_create and hb_shape -- and drops the AAT and Graphite shapers,
# the serialisers and the drawing API.
#
# One library, not two. harfbuzz-subset.cc looks like a companion to
# harfbuzz.cc but is very nearly a superset of it: both pull in hb-blob,
# hb-face, hb-shape and the whole hb-ot-* set, so linking both is a wall of
# duplicate symbols. What harfbuzz-subset.cc lacks is hb-ft.cc, the FreeType
# bridge -- so the shaping and subsetting we need is that amalgam plus that one
# file. A build that embeds no fonts pays nothing for the subsetter anyway,
# since nothing then references hb_subset_* and -flto drops it.
add_library(mscz_harfbuzz STATIC
    ${MSCZ_THIRDPARTY}/harfbuzz/src/harfbuzz-subset.cc
    ${MSCZ_THIRDPARTY}/harfbuzz/src/hb-ft.cc
)
target_compile_definitions(mscz_harfbuzz PUBLIC HB_TINY HAVE_FREETYPE=1 HB_NO_MT)
target_include_directories(mscz_harfbuzz PUBLIC
    ${MSCZ_THIRDPARTY}/harfbuzz/src
    # hb-ft.h is a public header and includes ft2build.h, so anything that
    # includes hb-ft.h needs FreeType's headers too.
    ${FREETYPE_INCLUDE_DIRS}
)
target_compile_features(mscz_harfbuzz PRIVATE cxx_std_17)

set(HARFBUZZ_LIBRARIES mscz_harfbuzz)
set(HARFBUZZ_INCLUDE_DIRS ${MSCZ_THIRDPARTY}/harfbuzz/src)

# --- zlib ---------------------------------------------------------------------

# A .mscz is a zip, so inflate is not optional. Upstream's wasm build expects
# the emscripten zlib port to be linked in globally, which cannot be fetched
# here; global/CMakeLists.txt links this instead.
#
# Built with its own sources listed rather than through zlib's CMakeLists,
# which also builds a shared library, examples and minigzip, and wants to
# install them.
set(MSCZ_ZLIB_SRC
    adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c
    gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c
    zutil.c
)
list(TRANSFORM MSCZ_ZLIB_SRC PREPEND ${MSCZ_THIRDPARTY}/zlib/)

add_library(mscz_zlib STATIC ${MSCZ_ZLIB_SRC})
target_include_directories(mscz_zlib PUBLIC ${MSCZ_THIRDPARTY}/zlib)
# zlib's own configure defines these when the platform has them; without them
# it falls back to its own stdio-free paths and warns about fdopen.
target_compile_definitions(mscz_zlib PRIVATE HAVE_UNISTD_H HAVE_STDARG_H)

# Not our code and not our warnings.
foreach(tgt freetype mscz_harfbuzz mscz_zlib)
    target_compile_options(${tgt} PRIVATE -w)
endforeach()
