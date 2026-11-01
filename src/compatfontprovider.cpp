/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "compatfontprovider.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "global/io/file.h"
#include "global/log.h"
#include "global/modularity/ioc.h"

#include "draw/internal/ifontface.h"
#include "draw/internal/ifontsdatabase.h"
#include "draw/types/fontstypes.h"

#include "draw/internal/fontfaceft.h"

using namespace muse;
using namespace muse::draw;

namespace {

//! The DPI QFontProvider's FontPaintDevice reports, which is what turns a
//! point size into pixels for every metric the layout is built on. Same value
//! as mu::engraving::DPI; named here because draw must not depend on
//! engraving.
constexpr double kDeviceDpi = 1200.0;

//! FontFaceFT's symbol mode scales a glyph's box and advance by constants its
//! comments derive from dpi_f = 5.0, which is muse::draw::DPI / 72:
//!
//!     advance: linearHoriAdvance * dpi_f / 655360  ->  / 2048
//!     bbox:    640.0 / dpi_f                       ->  / 2
//!
//! Both are baked in, and both are wrong for a provider measuring at 1200 dpi.
//! Measured against QFontProvider, the advance and the box both come out
//! exactly half -- 1.9974 to 2.0276 across sizes and glyphs, the spread being
//! rounding -- while every non-symbol metric already agrees. So the correction
//! is a factor of two, applied here rather than in FontFaceFT, whose constants
//! are right for the DPI they were written for.
constexpr double kSymbolMetricScale = 2.0;

//! QFont::setPointSizeF scales by the device DPI; QFont::setPixelSize does
//! not. Mirrors Font::toQFont, which prefers point size when both are set.
int pixelSizeFor(const Font& f)
{
    if (f.pointSizeF() > 0) {
        return static_cast<int>(std::lround(f.pointSizeF() * kDeviceDpi / 72.0));
    }
    if (f.pixelSize() > 0) {
        return f.pixelSize();
    }
    // Qt's default point size, through the same device.
    return static_cast<int>(std::lround(12.0 * kDeviceDpi / 72.0));
}

} // namespace

namespace mscz {

struct CompatFontProvider::Impl {
    //! One face per family-and-size, since FreeType metrics come out at
    //! whatever size the face was set to.
    struct Key {
        std::string path;
        int pixelSize = 0;
        bool symbolMode = false;

        bool operator<(const Key& o) const
        {
            if (path != o.path) { return path < o.path; }
            if (pixelSize != o.pixelSize) { return pixelSize < o.pixelSize; }
            return symbolMode < o.symbolMode;
        }
    };

    std::map<Key, std::unique_ptr<FontFaceFT> > faces;
    //! Font files, read once; FT_New_Memory_Face does not copy.
    std::map<std::string, ByteArray> files;
    //! Symbol fonts registered by family, as QFontProvider keeps them.
    std::map<String, io::path_t> symbolFonts;

    muse::GlobalInject<IFontsDatabase> fontsDatabase;

    //! A music font is read in symbol mode, which is how FontFaceFT knows to
    //! use the outline bounding box rather than the rasterised one.
    static bool isSymbolFont(const Font& f)
    {
        return f.type() == Font::Type::MusicSymbol || f.type() == Font::Type::MusicSymbolText;
    }

    FontFaceFT* face(const Font& f)
    {
        const std::shared_ptr<IFontsDatabase> fdb = fontsDatabase();
        if (!fdb) {
            return nullptr;
        }

        const FontDataKey dataKey(f.family(), f.bold(), f.italic());
        const io::path_t path = fdb->fontPath(dataKey, f.type());
        if (path.empty()) {
            return nullptr;
        }

        Key key;
        key.path = path.toStdString();
        key.pixelSize = pixelSizeFor(f);
        key.symbolMode = isSymbolFont(f);

        auto it = faces.find(key);
        if (it != faces.end()) {
            return it->second.get();
        }

        ByteArray& data = files[key.path];
        if (data.empty()) {
            io::File file(path);
            if (!file.open(io::IODevice::ReadOnly)) {
                LOGE() << "could not open font " << path;
                return nullptr;
            }
            data = file.readAll();
            file.close();
        }

        FaceKey faceKey(dataKey, f.type(), key.pixelSize);
        auto created = std::make_unique<FontFaceFT>();
        if (!created->load(faceKey, path, key.symbolMode)) {
            LOGE() << "FreeType could not read font " << path;
            return nullptr;
        }

        FontFaceFT* raw = created.get();
        faces[key] = std::move(created);
        return raw;
    }
};

CompatFontProvider::CompatFontProvider()
    : m_impl(new Impl())
{
}

CompatFontProvider::~CompatFontProvider() = default;

int CompatFontProvider::addSymbolFont(const String& family, const io::path_t& path)
{
    // QFontProvider also hands the file to QFontDatabase here. There is no
    // database to register with; remembering the family is all that is needed,
    // since the fonts database already knows the path.
    m_impl->symbolFonts[family] = path;
    return 0;
}

// --- vertical metrics --------------------------------------------------------
//
// Qt's QFontMetricsF reports these in device pixels at the device's DPI, which
// is what a face set to pixelSizeFor() gives, so these are direct.

double CompatFontProvider::ascent(const Font& f) const
{
    FontFaceFT* face = m_impl->face(f);
    return face ? from_f26d6(face->ascent()) : 0.0;
}

double CompatFontProvider::descent(const Font& f) const
{
    FontFaceFT* face = m_impl->face(f);
    return face ? from_f26d6(face->descent()) : 0.0;
}

double CompatFontProvider::xHeight(const Font& f) const
{
    FontFaceFT* face = m_impl->face(f);
    return face ? from_f26d6(face->xHeight()) : 0.0;
}

double CompatFontProvider::capHeight(const Font& f) const
{
    // QFontProvider uses QFontMetrics, not QFontMetricsF, for this one, so the
    // value it returns is a whole number of pixels.
    FontFaceFT* face = m_impl->face(f);
    return face ? std::round(from_f26d6(face->capHeight())) : 0.0;
}

double CompatFontProvider::height(const Font& f) const
{
    // QFontMetricsF::height() is ascent + descent.
    FontFaceFT* face = m_impl->face(f);
    if (!face) {
        return 0.0;
    }
    return from_f26d6(face->ascent()) + from_f26d6(face->descent());
}

double CompatFontProvider::lineSpacing(const Font& f) const
{
    // QFontMetricsF::lineSpacing() is leading() + height().
    FontFaceFT* face = m_impl->face(f);
    if (!face) {
        return 0.0;
    }
    return from_f26d6(face->leading() + face->ascent() + face->descent());
}

// --- glyph presence ----------------------------------------------------------

bool CompatFontProvider::inFont(const Font& f, char32_t ucs4) const
{
    FontFaceFT* face = m_impl->face(f);
    if (!face) {
        return false;
    }

    const glyph_idx_t idx = face->glyphIndex(ucs4);
    if (idx == 0) {
        return false;
    }

    // Some symbols are in a font's character map without having a glyph --
    // U+EE80 in Bravura -- so the box has to be valid too, as QFontProvider
    // checks with QRawFont.
    // FBBox is a RectX, already in SVG's orientation: FontFaceFT negates y
    // when it builds it, so top is above the baseline and height is positive.
    const FBBox box = face->glyphBbox(idx);
    return box.width() > 0 && box.height() > 0;
}

// --- advances ----------------------------------------------------------------

double CompatFontProvider::horizontalAdvance(const Font& f, const String& string) const
{
    if (string.empty()) {
        return 0.0;
    }

    FontFaceFT* face = m_impl->face(f);
    if (!face) {
        return 0.0;
    }

    // Shaped, so kerning and ligatures count, as they do in Qt.
    const std::u32string text = string.toStdU32String();
    const std::vector<GlyphPos> glyphs = face->glyphs(text.data(), static_cast<int>(text.size()));

    f26dot6_t advance = 0;
    for (const GlyphPos& g : glyphs) {
        advance += g.x_advance;
    }

    double result = from_f26d6(advance);
    if (Impl::isSymbolFont(f)) {
        result *= kSymbolMetricScale;
    }
    return result;
}

double CompatFontProvider::horizontalAdvance(const Font& f, char32_t ucs4) const
{
    // QFontProvider sends anything needing surrogates through the string
    // overload; here the shaping path is the same either way, so one route
    // serves both.
    return horizontalAdvance(f, String::fromUcs4(ucs4));
}

// --- bounding boxes ----------------------------------------------------------

muse::RectF CompatFontProvider::boundingRect(const Font& f, const String& string) const
{
    if (string.empty()) {
        return RectF();
    }

    FontFaceFT* face = m_impl->face(f);
    if (!face) {
        return RectF();
    }

    const std::u32string text = string.toStdU32String();
    const std::vector<GlyphPos> glyphs = face->glyphs(text.data(), static_cast<int>(text.size()));

    // The union of the glyph boxes, each at its shaped position. The boxes are
    // relative to the baseline with y already pointing down, so the top comes
    // out negative, which is what Qt returns too.
    double pen = 0.0;
    bool any = false;
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;

    const double scale = Impl::isSymbolFont(f) ? kSymbolMetricScale : 1.0;

    for (const GlyphPos& g : glyphs) {
        const FBBox box = face->glyphBbox(g.idx);
        const double x0 = pen + from_f26d6(box.left()) * scale;
        const double x1 = pen + from_f26d6(box.right()) * scale;
        const double y0 = from_f26d6(box.top()) * scale;
        const double y1 = from_f26d6(box.bottom()) * scale;

        if (!any) {
            left = x0;
            right = x1;
            top = y0;
            bottom = y1;
            any = true;
        } else {
            left = std::min(left, x0);
            right = std::max(right, x1);
            top = std::min(top, y0);
            bottom = std::max(bottom, y1);
        }

        pen += from_f26d6(g.x_advance) * scale;
    }

    if (!any) {
        return RectF();
    }
    return RectF(left, top, right - left, bottom - top);
}

muse::RectF CompatFontProvider::boundingRect(const Font& f, char32_t ucs4) const
{
    FontFaceFT* face = m_impl->face(f);
    if (!face) {
        return RectF();
    }

    const glyph_idx_t idx = face->glyphIndex(ucs4);
    if (idx == 0) {
        return RectF();
    }

    // For a music symbol QFontProvider deliberately avoids the rasterised
    // metrics, building a path at four times the size and scaling the vector
    // box down, because the discretised box is not accurate enough. Loading
    // the face in symbol mode gets the outline box straight from FreeType, so
    // there is nothing to upscale and nothing to discretise.
    const double scale = Impl::isSymbolFont(f) ? kSymbolMetricScale : 1.0;
    const FBBox box = face->glyphBbox(idx);
    return RectF(from_f26d6(box.left()) * scale, from_f26d6(box.top()) * scale,
                 from_f26d6(box.width()) * scale, from_f26d6(box.height()) * scale);
}

muse::RectF CompatFontProvider::tightBoundingRect(const Font& f, const String& string) const
{
    const RectF rect = boundingRect(f, string);

    // QFontProvider turns an invalid rect into an empty one, because Qt could
    // return garbage here and it corrupted layout. Keep the same guard: a
    // negative or absent extent is not something a caller should have to
    // handle.
    if (rect.width() <= 0.0 || rect.height() <= 0.0) {
        return RectF();
    }
    return rect;
}

} // namespace mscz
