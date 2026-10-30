/* What does FreeType actually cost in a wasm build, when all we ask of it is
 * glyph outlines and metrics?
 *
 * Also doubles as the worked example of FT_Outline_Decompose that the SVG
 * provider's path mode needs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

static char g_path[4096];
static size_t g_len;

static int moveTo(const FT_Vector* to, void* user)
{
    (void)user;
    g_len += snprintf(g_path + g_len, sizeof(g_path) - g_len, "M%.1f %.1f ", to->x / 64.0, -to->y / 64.0);
    return 0;
}

static int lineTo(const FT_Vector* to, void* user)
{
    (void)user;
    g_len += snprintf(g_path + g_len, sizeof(g_path) - g_len, "L%.1f %.1f ", to->x / 64.0, -to->y / 64.0);
    return 0;
}

static int conicTo(const FT_Vector* c, const FT_Vector* to, void* user)
{
    (void)user;
    g_len += snprintf(g_path + g_len, sizeof(g_path) - g_len, "Q%.1f %.1f %.1f %.1f ",
                      c->x / 64.0, -c->y / 64.0, to->x / 64.0, -to->y / 64.0);
    return 0;
}

static int cubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user)
{
    (void)user;
    g_len += snprintf(g_path + g_len, sizeof(g_path) - g_len, "C%.1f %.1f %.1f %.1f %.1f %.1f ",
                      c1->x / 64.0, -c1->y / 64.0, c2->x / 64.0, -c2->y / 64.0, to->x / 64.0, -to->y / 64.0);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: probe <font> <hex-codepoint>\n");
        return 2;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc((size_t)size);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        return 1;
    }
    fclose(f);

    FT_Library lib;
    if (FT_Init_FreeType(&lib)) {
        fprintf(stderr, "FT_Init_FreeType failed\n");
        return 1;
    }

    FT_Face face;
    if (FT_New_Memory_Face(lib, data, (FT_Long)size, 0, &face)) {
        fprintf(stderr, "FT_New_Memory_Face failed\n");
        return 1;
    }

    printf("family=%s style=%s glyphs=%ld upem=%d\n",
           face->family_name ? face->family_name : "?",
           face->style_name ? face->style_name : "?",
           (long)face->num_glyphs, face->units_per_EM);

    /* Outlines want a size set; 1000 px keeps the 26.6 numbers readable. */
    FT_Set_Char_Size(face, 1000 << 6, 1000 << 6, 0, 0);

    const FT_ULong code = strtoul(argv[2], NULL, 16);
    const FT_UInt idx = FT_Get_Char_Index(face, code);
    printf("U+%04lX -> glyph %u\n", (unsigned long)code, idx);
    if (!idx) {
        return 1;
    }

    if (FT_Load_Glyph(face, idx, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING)) {
        fprintf(stderr, "FT_Load_Glyph failed\n");
        return 1;
    }

    FT_Outline_Funcs funcs = { moveTo, lineTo, conicTo, cubicTo, 0, 0 };
    g_len = 0;
    if (FT_Outline_Decompose(&face->glyph->outline, &funcs, NULL)) {
        fprintf(stderr, "FT_Outline_Decompose failed\n");
        return 1;
    }

    printf("advance=%ld contours=%d points=%d\n",
           (long)face->glyph->metrics.horiAdvance,
           face->glyph->outline.n_contours, face->glyph->outline.n_points);
    printf("path (%zu chars): %.200s\n", g_len, g_path);

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    free(data);
    return 0;
}
