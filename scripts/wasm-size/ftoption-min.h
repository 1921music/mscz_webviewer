/* FreeType options for this project.
 *
 * Companion to ftmodule-min.h, which picks the drivers; this switches off
 * features inside the drivers we do keep. Both are pointed at from
 * cmake/MsczThirdparty.cmake.
 *
 * What we ask of FreeType: open an OTF/CFF or TrueType face from memory, read
 * glyph outlines in font units, and report metrics. We never rasterise -- the
 * output is SVG paths, and even in <text> mode the browser does the
 * rasterising -- and we never open a file, since resources arrive as bytes.
 *
 * Measured in the binary before this: TT_RunIns, the TrueType bytecode
 * interpreter, 13.6 KB; FT_Gzip_Uncompress 8.8 KB. Both are for things we do
 * not do. Note that FT_DISABLE_ZLIB in FreeType's CMake only stops it using
 * the *system* zlib -- it then falls back to its own bundled gzip, which is
 * the opposite of what we want, so the option has to be turned off here.
 */

#include <freetype/config/ftoption.h>

/* Hinting is for rasterising. We take outlines in font units and scale them
 * ourselves, so the bytecode interpreter never runs. */
#undef TT_CONFIG_OPTION_BYTECODE_INTERPRETER
#undef TT_CONFIG_OPTION_SUBPIXEL_HINTING

/* Compressed font containers. Nothing we load is gzipped or LZW'd, and the
 * drivers that would want them (PCF, BDF) are not in ftmodule-min.h. */
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_LZW

/* Mac resource forks and suitcase fonts. */
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK

/* The incremental interface is for a client that supplies glyphs on demand,
 * which is a PostScript printing idea. */
#undef FT_CONFIG_OPTION_INCREMENTAL

/* The OT-SVG driver hands SVG documents to a renderer we do not have. Ours is
 * the other direction. */
#undef FT_CONFIG_OPTION_SVG
