/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "glyphoutlines.h"

#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ot.h>

#include "global/io/file.h"
#include "global/log.h"
#include "global/modularity/ioc.h"

#include "draw/internal/ifontsdatabase.h"

using namespace muse;
using namespace muse::draw;

namespace {

//! Font units are integers in a face's own grid -- 1000 per em for the CFF
//! faces here -- so there is nothing to round and the path stays compact.
std::string num(long v)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%ld", v);
    return std::string(buf);
}

struct Sink {
    std::ostringstream d;
    //! FreeType's y points up and SVG's points down.
    long x(const FT_Vector* v) const { return v->x; }
    long y(const FT_Vector* v) const { return -v->y; }
};

int moveTo(const FT_Vector* to, void* user)
{
    Sink* s = static_cast<Sink*>(user);
    s->d << "M" << num(s->x(to)) << " " << num(s->y(to));
    return 0;
}

int lineTo(const FT_Vector* to, void* user)
{
    Sink* s = static_cast<Sink*>(user);
    s->d << "L" << num(s->x(to)) << " " << num(s->y(to));
    return 0;
}

int conicTo(const FT_Vector* c, const FT_Vector* to, void* user)
{
    Sink* s = static_cast<Sink*>(user);
    s->d << "Q" << num(s->x(c)) << " " << num(s->y(c))
         << " " << num(s->x(to)) << " " << num(s->y(to));
    return 0;
}

int cubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user)
{
    Sink* s = static_cast<Sink*>(user);
    s->d << "C" << num(s->x(c1)) << " " << num(s->y(c1))
         << " " << num(s->x(c2)) << " " << num(s->y(c2))
         << " " << num(s->x(to)) << " " << num(s->y(to));
    return 0;
}

} // namespace

namespace mscz {

struct GlyphOutlines::Face {
    FT_Face face = nullptr;
    hb_font_t* shapingFont = nullptr;
    //! FT_New_Memory_Face does not copy, so the bytes have to outlive the face.
    ByteArray data;
    int unitsPerEm = 1000;
    std::unordered_map<char32_t, Glyph> cache;
};

struct GlyphOutlines::Impl {
    FT_Library library = nullptr;
    std::map<std::string, Face> faces;
    Glyph invalid;

    muse::GlobalInject<IFontsDatabase> fontsDatabase;
};

GlyphOutlines::GlyphOutlines()
    : m_impl(new Impl())
{
    if (FT_Init_FreeType(&m_impl->library) != 0) {
        LOGE() << "could not initialise FreeType";
        m_impl->library = nullptr;
    }
}

GlyphOutlines::~GlyphOutlines()
{
    for (auto& [_, face] : m_impl->faces) {
        if (face.shapingFont) {
            hb_font_destroy(face.shapingFont);
        }
        if (face.face) {
            FT_Done_Face(face.face);
        }
    }
    if (m_impl->library) {
        FT_Done_FreeType(m_impl->library);
    }
    delete m_impl;
}

GlyphOutlines::Face* GlyphOutlines::faceFor(const Font& font)
{
    if (!m_impl->library) {
        return nullptr;
    }

    const std::shared_ptr<IFontsDatabase> fdb = m_impl->fontsDatabase();
    if (!fdb) {
        return nullptr;
    }

    const FontDataKey key(font.family(), font.bold(), font.italic());
    const io::path_t path = fdb->fontPath(key, font.type());
    if (path.empty()) {
        return nullptr;
    }

    const std::string pathKey = path.toStdString();
    auto it = m_impl->faces.find(pathKey);
    if (it != m_impl->faces.end()) {
        return it->second.face ? &it->second : nullptr;
    }

    Face& face = m_impl->faces[pathKey];

    io::File file(path);
    if (!file.open(io::IODevice::ReadOnly)) {
        LOGE() << "could not open font " << path;
        return nullptr;
    }
    face.data = file.readAll();
    file.close();

    if (FT_New_Memory_Face(m_impl->library, face.data.constData(),
                           static_cast<FT_Long>(face.data.size()), 0, &face.face) != 0) {
        LOGE() << "FreeType could not read font " << path;
        face.face = nullptr;
        return nullptr;
    }

    face.unitsPerEm = face.face->units_per_EM > 0 ? face.face->units_per_EM : 1000;
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(face.data.constData()),
                                    static_cast<unsigned>(face.data.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* hbFace = hb_face_create(blob, 0);
    face.shapingFont = hb_font_create(hbFace);
    hb_ot_font_set_funcs(face.shapingFont);
    hb_font_set_scale(face.shapingFont, face.unitsPerEm, face.unitsPerEm);
    hb_face_destroy(hbFace);
    hb_blob_destroy(blob);
    return &face;
}

double GlyphOutlines::advanceEm(const Font& font, const String& text)
{
    Face* face = faceFor(font);
    if (!face || text.empty()) {
        return 0.0;
    }
    const std::u32string u32 = text.toStdU32String();
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf32(buffer, reinterpret_cast<const uint32_t*>(u32.data()),
                        static_cast<int>(u32.size()), 0, static_cast<int>(u32.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(face->shapingFont, buffer, nullptr, 0);
    unsigned count = 0;
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    double advance = 0;
    for (unsigned i = 0; i < count; ++i) {
        advance += positions[i].x_advance;
    }
    hb_buffer_destroy(buffer);
    return advance / face->unitsPerEm;
}

const GlyphOutlines::Glyph& GlyphOutlines::glyph(const Font& font, char32_t ucs4)
{
    Face* face = faceFor(font);
    if (!face) {
        return m_impl->invalid;
    }

    auto cached = face->cache.find(ucs4);
    if (cached != face->cache.end()) {
        return cached->second;
    }

    Glyph& glyph = face->cache[ucs4];
    glyph.unitsPerEm = face->unitsPerEm;

    const FT_UInt index = FT_Get_Char_Index(face->face, ucs4);
    if (index == 0) {
        return glyph;
    }

    // NO_SCALE keeps the outline in font units, so one path works at any size;
    // NO_HINTING because hinting is for pixel grids and this is not going to
    // one.
    if (FT_Load_Glyph(face->face, index, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0) {
        return glyph;
    }

    FT_Outline& outline = face->face->glyph->outline;
    if (outline.n_contours == 0) {
        // A blank glyph, a space for instance. Valid, just nothing to draw.
        glyph.valid = true;
        return glyph;
    }

    Sink sink;
    FT_Outline_Funcs funcs = { moveTo, lineTo, conicTo, cubicTo, 0, 0 };
    if (FT_Outline_Decompose(&outline, &funcs, &sink) != 0) {
        return glyph;
    }

    glyph.path = sink.d.str();
    glyph.valid = !glyph.path.empty();
    return glyph;
}

} // namespace mscz
