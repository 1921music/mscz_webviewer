/* Minimal FreeType module set for this project.
 *
 * We read OTF/CFF (Leland, Edwin) and TrueType, we want outlines and metrics,
 * and we never rasterise -- the output is SVG paths, and even in <text> mode
 * the browser does the rasterising. So: sfnt plus the CFF and TrueType
 * drivers, the PostScript helpers CFF charstrings need, and nothing else.
 *
 * Left out: Type1, CID, Type42, PFR, Windows FNT, PCF, BDF, the OT-SVG driver,
 * the autofitter, and the smooth and monochrome rasterisers.
 */

FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
