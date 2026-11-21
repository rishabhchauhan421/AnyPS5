# libSceFreeTypeOt

## change The FT_Glyph_Transform, FT_Glyph_StrokeBorder, FT_Glyph_To_Bitmap and FT_New_Library NIDs

- The FT_Glyph_Transform, FT_Glyph_To_Bitmap and FT_New_Library NIDs (`AVEEb57HhUw`, `c1nbr8oSWlg`, `yrtsLmTfa48`) in [Export.cpp](../../../core/libs/prx/libSceFreeTypeOt/Export.cpp) are identified, but their provider-specific prototypes and behavior remain unverified. Their named helpers retain the explicit inventory aliases and throw; the separate libSceFreeType implementations do not supply this provider.

## add Functional

- [FT_Glyph_Stroke and FT_Glyph_StrokeBorder](../../../core/libs/prx/libSceFreeTypeOt/Export.cpp) (`0rKmI1ZbMAc`, `BBRhYfYBDgU`) - the console prototypes are assumed to be FreeType's, which are unchanged from 2.4.11 to 2.15.0. They copy the glyph, parse its outline with the stroker and export all borders or the inside or outside border into the copy, following [FreeType 2.15.0](https://github.com/freetype/freetype/blob/VER-2-15-0/src/base/ftstroke.c) on the bundled stroker; output is not compared with a console or with the provider's 2.4.11 code. A null glyph pointer, a null glyph or a bitmap glyph returns `FT_Err_Invalid_Argument` without touching the pointer, and an unwritable glyph pointer throws.
