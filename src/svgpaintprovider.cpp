/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "svgpaintprovider.h"
#include "textfontbackend.h"

#include <cmath>
#include <cstdio>
#include <iomanip>

#include "draw/types/fontstypes.h"
#include "draw/types/painterpath.h"

using namespace muse;
using namespace muse::draw;

namespace {

//! A point is 1/72 inch and a user unit is 1/engraving::DPI inch, with
//! engraving::DPI 360, so a point size covers DPI_F user units. Same constant
//! the font engine uses.
constexpr double kFontSizeScale = muse::draw::DPI_F;

const char* lineCapName(PenCapStyle style)
{
    switch (style) {
    case PenCapStyle::FlatCap: return "butt";
    case PenCapStyle::SquareCap: return "square";
    case PenCapStyle::RoundCap: return "round";
    }
    return "butt";
}

const char* lineJoinName(PenJoinStyle style)
{
    switch (style) {
    case PenJoinStyle::MiterJoin: return "miter";
    case PenJoinStyle::BevelJoin: return "bevel";
    case PenJoinStyle::RoundJoin: return "round";
    }
    return "miter";
}

// MuseJazz Text's legacy chord alphabet predates SMuFL. Its old PUA
// codepoints mean different symbols in Leland, so substitute by meaning.
// Source: engraving/data/chords/{chords_jazz,chords_std}.xml.
char32_t museJazzChordFallback(char32_t cp)
{
    switch (cp) {
    case 0xE10C: return 0xED62; // sharp
    case 0xE10D: return 0xED60; // flat
    case 0xE10E: return 0xED61; // natural
    case 0xE186: return 0xE872; // augmented
    case 0xE18A: return 0xE873; // major seventh
    case 0xE18E: return 0xE870; // diminished
    case 0xE18F: return 0xE871; // half diminished
    case 0xE263: return 0xED63; // double sharp
    case 0xE264: return 0xED64; // double flat
    default: return cp;
    }
}

} // namespace

namespace mscz {

SvgPaintProvider::SvgPaintProvider(SymbolMode symbolMode, FontMode fontMode)
    : m_symbolMode(symbolMode), m_fontMode(fontMode)
{
}
SvgPaintProvider::~SvgPaintProvider() = default;

std::string SvgPaintProvider::svg() const
{
    return m_svg;
}

// --- lifecycle ---------------------------------------------------------------

bool SvgPaintProvider::isActive() const
{
    return m_active;
}

void SvgPaintProvider::beginTarget(const std::string& name)
{
    m_active = true;
    m_name = name;
    m_body.str(std::string());
    m_defs.str(std::string());
    m_stack.clear();
    m_state = State();
    m_nextClipId = 0;
    m_glyphIds.clear();
    m_usedGlyphs.clear();
    m_openObjectGroups = 0;
    m_stateGroupOpen = false;
    m_stateGroupAttrs.clear();
    m_svg.clear();
}

void SvgPaintProvider::beforeEndTargetHook(Painter*)
{
}

bool SvgPaintProvider::endTarget(bool)
{
    closeStateGroup();
    while (m_openObjectGroups > 0) {
        m_body << "</g>\n";
        --m_openObjectGroups;
    }

    // The viewBox is the viewport, not the window. Painter::setWindow folds the
    // window-to-viewport mapping into the transform it hands us and passes the
    // window itself along only "for debug purpose", so the coordinates arriving
    // here are already in device space.
    const double viewBoxW = m_viewport.width() > 0.0 ? m_viewport.width() : m_window.width();
    const double viewBoxH = m_viewport.height() > 0.0 ? m_viewport.height() : m_window.height();

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\""
        << " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
        // No width or height: the viewBox alone makes the document scale to
        // whatever box a viewer puts it in, which is what a web viewer wants.
        << " viewBox=\"0 0 " << formatNumber(viewBoxW) << " " << formatNumber(viewBoxH) << "\">\n";

    // Subsetting happens here rather than as the glyphs are drawn, because it
    // needs the complete set of code points the page turned out to use.
    const std::string style = fontFaceStyle();
    if (!style.empty()) {
        out << style;
    }

    const std::string defs = m_defs.str();
    if (!defs.empty()) {
        out << "<defs>\n" << defs << "</defs>\n";
    }

    out << m_body.str()
        << "</svg>\n";

    m_svg = out.str();
    m_active = false;
    return true;
}

void SvgPaintProvider::beginObject(const std::string& name)
{
    // Kept as a group so the output can be read against the score: the engine
    // names these after the element being drawn.
    closeStateGroup();
    m_body << "<g data-object=\"" << name << "\">\n";
    ++m_openObjectGroups;
}

void SvgPaintProvider::endObject()
{
    closeStateGroup();
    if (m_openObjectGroups > 0) {
        m_body << "</g>\n";
        --m_openObjectGroups;
    }
}

// --- state -------------------------------------------------------------------

void SvgPaintProvider::setAntialiasing(bool arg)
{
    m_state.antialiasing = arg;
}

void SvgPaintProvider::setCompositionMode(CompositionMode)
{
    // SourceOver is the SVG default and the only mode the score paint path
    // uses in anger; HardLight shows up for highlight overlays we do not draw.
}

void SvgPaintProvider::setWindow(const RectF& window)
{
    m_window = window;
}

void SvgPaintProvider::setViewport(const RectF& viewport)
{
    m_viewport = viewport;
}

void SvgPaintProvider::setFont(const Font& font)
{
    m_state.font = font;
}

const Font& SvgPaintProvider::font() const
{
    return m_state.font;
}

void SvgPaintProvider::setPen(const Pen& pen)
{
    m_state.pen = pen;
}

void SvgPaintProvider::setNoPen()
{
    m_state.pen.setStyle(PenStyle::NoPen);
}

const Pen& SvgPaintProvider::pen() const
{
    return m_state.pen;
}

void SvgPaintProvider::setBrush(const Brush& brush)
{
    m_state.brush = brush;
}

const Brush& SvgPaintProvider::brush() const
{
    return m_state.brush;
}

void SvgPaintProvider::save()
{
    m_stack.push_back(m_state);
}

void SvgPaintProvider::restore()
{
    if (!m_stack.empty()) {
        m_state = m_stack.back();
        m_stack.pop_back();
    }
}

double SvgPaintProvider::deviceLogicalDpi() const
{
    return muse::draw::DPI;
}

void SvgPaintProvider::setTransform(const Transform& transform)
{
    m_state.transform = transform;
}

const Transform& SvgPaintProvider::transform() const
{
    return m_state.transform;
}

bool SvgPaintProvider::hasClipping() const
{
    return m_state.clipping;
}

void SvgPaintProvider::setClipRect(const RectF& rect)
{
    m_state.clipRect = rect;
    m_state.clipTransform = m_state.transform;
    m_state.clipId.clear();
}

void SvgPaintProvider::setMask(const RectF&, const std::vector<RectF>&)
{
    // Only used for the mask-layout feature, which hides parts of items behind
    // others. Ignoring it draws both, which is visible but not wrong-shaped.
}

void SvgPaintProvider::setClipping(bool enable)
{
    m_state.clipping = enable;
}

// --- attribute helpers -------------------------------------------------------

std::string SvgPaintProvider::formatNumber(double value)
{
    if (!std::isfinite(value)) {
        return "0";
    }
    // Two decimals is well under a thousandth of a staff space at 360 units
    // per inch, and keeps the files far smaller than full precision would.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    std::string s(buf);
    // Trim the trailing zeros and any bare point.
    const size_t dot = s.find('.');
    if (dot != std::string::npos) {
        size_t last = s.size();
        while (last > dot + 1 && s[last - 1] == '0') {
            --last;
        }
        if (last == dot + 1) {
            last = dot;
        }
        s.resize(last);
    }
    if (s == "-0") {
        s = "0";
    }
    return s;
}

std::string SvgPaintProvider::colorToHex(const Color& color)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", color.red(), color.green(), color.blue());
    return std::string(buf);
}

std::string SvgPaintProvider::escapeText(const String& text)
{
    const std::string utf8 = text.toStdString();
    std::string out;
    out.reserve(utf8.size());
    for (const char c : utf8) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string SvgPaintProvider::ensureClipPath()
{
    if (!m_state.clipping || !m_state.clipRect.isValid()) {
        return std::string();
    }
    if (!m_state.clipId.empty()) {
        return m_state.clipId;
    }

    char id[32];
    std::snprintf(id, sizeof(id), "clip%d", m_nextClipId++);
    m_state.clipId = id;

    m_defs << "<clipPath id=\"" << m_state.clipId << "\"><rect"
           << " x=\"" << formatNumber(m_state.clipRect.left()) << "\""
           << " y=\"" << formatNumber(m_state.clipRect.top()) << "\""
           << " width=\"" << formatNumber(m_state.clipRect.width()) << "\""
           << " height=\"" << formatNumber(m_state.clipRect.height()) << "\""
           << "/></clipPath>\n";

    return m_state.clipId;
}

std::string SvgPaintProvider::clipAttribute()
{
    const std::string id = ensureClipPath();
    if (id.empty()) {
        return std::string();
    }
    return " clip-path=\"url(#" + id + ")\"";
}

std::string SvgPaintProvider::transformAttribute() const
{
    const Transform& t = m_state.transform;
    // Transform::type() is private, so test the matrix directly.
    if (t.m11() == 1.0 && t.m12() == 0.0 && t.m21() == 0.0 && t.m22() == 1.0
        && t.dx() == 0.0 && t.dy() == 0.0) {
        return std::string();
    }

    std::ostringstream a;
    // A pure scale-and-translate, which is what the view transform and most
    // item transforms are, says the same thing in far fewer characters as
    // translate()+scale() than as a full matrix().
    if (t.m12() == 0.0 && t.m21() == 0.0) {
        a << " transform=\"";
        if (t.dx() != 0.0 || t.dy() != 0.0) {
            a << "translate(" << formatNumber(t.dx()) << " " << formatNumber(t.dy()) << ")";
        }
        if (t.m11() != 1.0 || t.m22() != 1.0) {
            if (t.m11() == t.m22()) {
                a << "scale(" << formatNumber(t.m11()) << ")";
            } else {
                a << "scale(" << formatNumber(t.m11()) << " " << formatNumber(t.m22()) << ")";
            }
        }
        a << "\"";
        return a.str();
    }

    a << " transform=\"matrix("
      << formatNumber(t.m11()) << " " << formatNumber(t.m12()) << " "
      << formatNumber(t.m21()) << " " << formatNumber(t.m22()) << " "
      << formatNumber(t.dx()) << " " << formatNumber(t.dy()) << ")\"";
    return a.str();
}

std::string SvgPaintProvider::paintAttributes(bool filled)
{
    const Pen& p = m_state.pen;
    const Brush& b = m_state.brush;

    std::ostringstream a;

    if (filled && b.style() != BrushStyle::NoBrush) {
        a << " fill=\"" << colorToHex(b.color()) << "\"";
        if (b.color().alpha() != 255) {
            a << " fill-opacity=\"" << formatNumber(b.color().alpha() / 255.0) << "\"";
        }
    } else {
        a << " fill=\"none\"";
    }

    if (p.style() == PenStyle::NoPen) {
        a << " stroke=\"none\"";
    } else {
        a << " stroke=\"" << colorToHex(p.color()) << "\""
          << " stroke-width=\"" << formatNumber(p.widthF()) << "\"";
        if (p.color().alpha() != 255) {
            a << " stroke-opacity=\"" << formatNumber(p.color().alpha() / 255.0) << "\"";
        }
        if (p.capStyle() != PenCapStyle::SquareCap) {
            a << " stroke-linecap=\"" << lineCapName(p.capStyle()) << "\"";
        }
        if (p.joinStyle() != PenJoinStyle::BevelJoin) {
            a << " stroke-linejoin=\"" << lineJoinName(p.joinStyle()) << "\"";
        }
        if (p.style() != PenStyle::SolidLine) {
            // Pen::dashPattern() already turns the named styles into numbers,
            // in units of the pen width, which is what SVG wants scaled by it.
            const std::vector<double> dashes = p.dashPattern();
            if (!dashes.empty()) {
                a << " stroke-dasharray=\"";
                for (size_t i = 0; i < dashes.size(); ++i) {
                    if (i) {
                        a << ",";
                    }
                    a << formatNumber(dashes[i] * p.widthF());
                }
                a << "\"";
            }
        }
    }

    if (!m_state.antialiasing) {
        a << " shape-rendering=\"crispEdges\"";
    }

    // No transform here. Every item has its own, so folding it into the group
    // key would give one group per element and defeat the grouping; it goes on
    // the element instead.
    a << clipAttribute();
    return a.str();
}

std::string SvgPaintProvider::textAttributes()
{
    std::ostringstream a;
    const auto backend = textFontBackend(m_state.font);
    a << " font-family=\"" << escapeText(backend ? backend->svgFamily(m_state.font) : m_state.font.family().id()) << "\""
      << " font-size=\"" << formatNumber(m_state.font.pointSizeF() * kFontSizeScale) << "\"";
    if (m_state.font.bold()) {
        a << " font-weight=\"bold\"";
    }
    if (m_state.font.italic()) {
        a << " font-style=\"italic\"";
    }
    if (backend) { a << " font-kerning=\"normal\""; }

    // Text takes the pen colour as its fill, the way QPainter draws it.
    const Pen& p = m_state.pen;
    a << " fill=\"" << colorToHex(p.style() == PenStyle::NoPen ? m_state.brush.color() : p.color()) << "\"";

    a << clipAttribute();
    return a.str();
}

void SvgPaintProvider::openStateGroup(const std::string& attrs)
{
    if (m_stateGroupOpen && m_stateGroupAttrs == attrs) {
        return;
    }
    closeStateGroup();
    if (attrs.empty()) {
        return;
    }
    m_body << "<g" << attrs << ">";
    m_stateGroupOpen = true;
    m_stateGroupAttrs = attrs;
}

void SvgPaintProvider::closeStateGroup()
{
    if (m_stateGroupOpen) {
        m_body << "</g>\n";
        m_stateGroupOpen = false;
        m_stateGroupAttrs.clear();
    }
}

void SvgPaintProvider::noteUsedGlyphs(const String& text)
{
    if (m_fontMode != FontMode::EmbedSubset || text.empty()) {
        return;
    }
    if (textFontBackend(m_state.font) && !containsMusicCharacters(text)) { return; }
    std::set<char32_t>& used = m_usedGlyphs[FontSubset::keyFor(m_state.font)];
    const std::u32string u32 = text.toStdU32String();
    used.insert(u32.begin(), u32.end());
}

std::string SvgPaintProvider::fontFaceStyle()
{
    if (m_fontMode != FontMode::EmbedSubset || m_usedGlyphs.empty()) {
        return std::string();
    }

    std::ostringstream css;
    for (const auto& [key, codepoints] : m_usedGlyphs) {
        const std::vector<std::uint8_t> font = m_subsetter.subset(key, codepoints);
        if (font.empty()) {
            // A family we do not ship. Nothing to embed; the reader will
            // substitute, exactly as it would without this block.
            continue;
        }

        css << "@font-face{font-family:\"" << key.family << "\";";
        if (key.bold) {
            css << "font-weight:bold;";
        }
        if (key.italic) {
            css << "font-style:italic;";
        }
        css << "src:url(data:font/" << (FontSubset::formatHint(font) == "opentype" ? "otf" : "ttf")
            << ";base64," << FontSubset::base64(font) << ") format(\""
            << FontSubset::formatHint(font) << "\")}\n";
    }

    const std::string body = css.str();
    if (body.empty()) {
        return std::string();
    }
    return "<style>\n" + body + "</style>\n";
}

// --- drawing -----------------------------------------------------------------

void SvgPaintProvider::drawPath(const PainterPath& path)
{
    const size_t count = path.elementCount();
    if (count == 0) {
        return;
    }

    std::ostringstream d;
    for (size_t i = 0; i < count; ++i) {
        const PainterPath::Element e = path.elementAt(i);
        switch (e.type) {
        case PainterPath::ElementType::MoveToElement:
            d << "M" << formatNumber(e.x) << " " << formatNumber(e.y) << " ";
            break;
        case PainterPath::ElementType::LineToElement:
            d << "L" << formatNumber(e.x) << " " << formatNumber(e.y) << " ";
            break;
        case PainterPath::ElementType::CurveToElement: {
            // A CurveTo is followed by two CurveToData points: the second
            // control point and the end point.
            d << "C" << formatNumber(e.x) << " " << formatNumber(e.y);
            for (int k = 0; k < 2 && i + 1 < count; ++k) {
                const PainterPath::Element next = path.elementAt(i + 1);
                if (next.type != PainterPath::ElementType::CurveToDataElement) {
                    break;
                }
                ++i;
                d << " " << formatNumber(next.x) << " " << formatNumber(next.y);
            }
            d << " ";
        } break;
        case PainterPath::ElementType::CurveToDataElement:
            // Consumed by the CurveTo above; a stray one means a malformed path.
            break;
        }
    }

    openStateGroup(paintAttributes(true));
    m_body << "<path d=\"" << d.str() << "\"";
    if (path.fillRule() == PainterPath::FillRule::OddEvenFill) {
        m_body << " fill-rule=\"evenodd\"";
    }
    m_body << transformAttribute() << "/>";
}

void SvgPaintProvider::drawPolygon(const PointF* points, size_t pointCount, PolygonMode mode)
{
    if (!points || pointCount == 0) {
        return;
    }

    std::ostringstream pts;
    for (size_t i = 0; i < pointCount; ++i) {
        if (i) {
            pts << " ";
        }
        pts << formatNumber(points[i].x()) << "," << formatNumber(points[i].y());
    }

    const bool polyline = (mode == PolygonMode::Polyline);
    openStateGroup(paintAttributes(!polyline));
    m_body << (polyline ? "<polyline points=\"" : "<polygon points=\"") << pts.str() << "\"";
    if (mode == PolygonMode::OddEven) {
        m_body << " fill-rule=\"evenodd\"";
    }
    m_body << transformAttribute() << "/>";
}

void SvgPaintProvider::drawText(const PointF& point, const String& text)
{
    if (text.empty()) {
        return;
    }

    // TextBase sends inline SMuFL symbols through drawText, not drawSymbol.
    // Split only at music symbols; ordinary text remains one shaped run.
    if (m_symbolMode == SymbolMode::Path) {
        const std::u32string chars = text.toStdU32String();
        size_t start = 0;
        for (size_t i = 0; i < chars.size(); ++i) {
            const char32_t cp = chars[i];
            if (!((cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0x1D100 && cp <= 0x1D1FF))) {
                continue;
            }
            const auto position = [&](size_t index) {
                const String prefix = String::fromUcs4(chars.data(), index);
                if (auto backend = textFontBackend(m_state.font); backend && !containsMusicCharacters(prefix)) {
                    return PointF(point.x() + backend->measure(m_state.font, prefix).advance
                                  * (72.0 / 1200.0) * kFontSizeScale, point.y());
                }
                return PointF(point.x() + m_glyphs.advanceEm(m_state.font, prefix)
                              * m_state.font.pointSizeF() * kFontSizeScale, point.y());
            };
            Font symbolFont = m_state.font;
            char32_t symbolCode = cp;
            if (!m_glyphs.glyph(symbolFont, cp).valid) {
                if (symbolFont.family() == Font::FontFamily(u"MuseJazz Text") && (cp == 0xE185 || cp == 0xE18B)) {
                    // These legacy glyphs are the words "sus" and "add".
                    // Keep the replacement words selectable, as in chords_std.
                    if (i > start) {
                        drawText(position(start), String::fromUcs4(chars.data() + start, i - start));
                    }
                    drawText(position(i), cp == 0xE185 ? String(u"sus") : String(u"add"));
                    start = i + 1;
                    continue;
                }
                // Chord names can label a music-text font as Text/Harmony.
                // If that family is absent, those types fall back to Edwin,
                // which has no SMuFL glyphs. Resolve this symbol as music text
                // so the database selects its bundled music-text fallback.
                symbolFont.setFamily(symbolFont.family(), Font::Type::MusicSymbolText);
                if (symbolFont.family() == Font::FontFamily(u"MuseJazz Text")) {
                    symbolCode = museJazzChordFallback(cp);
                }
                if (!m_glyphs.glyph(symbolFont, symbolCode).valid) {
                    // The text companion omits some full-size SMuFL symbols,
                    // such as coda. Try the bundled music face for those.
                    symbolFont.setFamily(u"Leland", Font::Type::MusicSymbol);
                    if (!m_glyphs.glyph(symbolFont, symbolCode).valid) {
                        continue;
                    }
                }
            }
            if (i > start) {
                drawText(position(start), String::fromUcs4(chars.data() + start, i - start));
            }
            const PointF symbolPosition = position(i);
            const Font saved = m_state.font;
            m_state.font = symbolFont;
            drawSymbolAsPath(symbolPosition, symbolCode);
            m_state.font = saved;
            start = i + 1;
        }
        if (start != 0) {
            if (start < chars.size()) {
                drawText(PointF(point.x() + m_glyphs.advanceEm(m_state.font, String::fromUcs4(chars.data(), start))
                               * m_state.font.pointSizeF() * kFontSizeScale, point.y()),
                         String::fromUcs4(chars.data() + start, chars.size() - start));
            }
            return;
        }
    }

    noteUsedGlyphs(text);

    // Qt places the baseline at the given point, and so does SVG <text> by
    // default, so the position carries over unchanged.
    openStateGroup(textAttributes());
    // Set this on text itself: SVG's text whitespace rules do not inherit a
    // group's preserve setting consistently when imported into an HTML page.
    // Canvas/MuseScore measure repeated and trailing spaces literally.
    m_body << "<text style=\"white-space:pre\"";
    if (point.x() != 0.0) {
        m_body << " x=\"" << formatNumber(point.x()) << "\"";
    }
    if (point.y() != 0.0) {
        m_body << " y=\"" << formatNumber(point.y()) << "\"";
    }
    m_body << transformAttribute() << ">" << escapeText(text) << "</text>";
}

void SvgPaintProvider::drawText(const RectF& rect, int, const String& text)
{
    // The alignment flags are not honoured yet; the score paint path reaches
    // here only for a few debug and header/footer cases.
    drawText(rect.topLeft(), text);
}

void SvgPaintProvider::drawTextWorkaround(const Font& f, const PointF& pos, const String& text)
{
    // The workaround exists to dodge a Qt DPI bug when drawing bold text under
    // a scaling transform. Nothing like it applies here.
    const Font saved = m_state.font;
    m_state.font = f;
    drawText(pos, text);
    m_state.font = saved;
}

bool SvgPaintProvider::drawSymbolAsPath(const PointF& point, char32_t ucs4Code)
{
    const GlyphOutlines::Glyph& glyph = m_glyphs.glyph(m_state.font, ucs4Code);
    if (!glyph.valid) {
        return false;
    }
    if (glyph.path.empty()) {
        return true;    // a blank glyph; nothing to draw, but not a failure
    }

    const auto key = std::make_pair(FontSubset::keyFor(m_state.font), ucs4Code);

    auto known = m_glyphIds.find(key);
    if (known == m_glyphIds.end()) {
        char id[48];
        std::snprintf(id, sizeof(id), "g%zu", m_glyphIds.size());
        known = m_glyphIds.emplace(key, id).first;
        m_defs << "<path id=\"" << id << "\" d=\"" << glyph.path << "\"/>\n";
    }

    // The path is in font units, so scale by size/em. The transforms compose
    // left to right, so the painter's own transform has to come first.
    const double size = m_state.font.pointSizeF() * kFontSizeScale;
    const double scale = size / static_cast<double>(glyph.unitsPerEm);

    // Fill comes from the pen, as it does for text.
    const Pen& p = m_state.pen;
    const Color colour = p.style() == PenStyle::NoPen ? m_state.brush.color() : p.color();

    openStateGroup(std::string(" fill=\"") + colorToHex(colour) + "\"" + clipAttribute());

    // One matrix rather than a transform list. The painter's transform, the
    // position and the font-units-to-size scale compose into the same thing,
    // and saying it once is a good deal shorter per <use> -- of which there are
    // hundreds on a page. Transform::translate and ::scale pre-multiply in
    // local space, which is the order an SVG transform list would apply them.
    Transform m = m_state.transform;
    m.translate(point.x(), point.y());
    m.scale(scale, scale);

    // The scale factors end up around 0.03, so formatNumber's two decimals
    // would quantise them to nothing; the translations are in device units
    // where two decimals is already far finer than a pixel.
    char buf[96];
    std::snprintf(buf, sizeof(buf), "matrix(%.6g %.6g %.6g %.6g %s %s)",
                  m.m11(), m.m12(), m.m21(), m.m22(),
                  formatNumber(m.dx()).c_str(), formatNumber(m.dy()).c_str());

    m_body << "<use href=\"#" << known->second << "\" transform=\"" << buf << "\"/>";
    return true;
}

void SvgPaintProvider::drawSymbol(const PointF& point, char32_t ucs4Code)
{
    if (m_symbolMode == SymbolMode::Path && drawSymbolAsPath(point, ucs4Code)) {
        return;
    }
    // Font mode, or the glyph could not be read.
    drawText(point, String::fromUcs4(&ucs4Code, 1));
}

void SvgPaintProvider::drawPixmap(const PointF&, const Pixmap&)
{
    // Only reached by scores embedding raster images.
}

void SvgPaintProvider::drawTiledPixmap(const RectF&, const Pixmap&, const PointF&)
{
}

#ifndef NO_QT_SUPPORT
void SvgPaintProvider::drawPixmap(const PointF&, const QPixmap&)
{
}

void SvgPaintProvider::drawTiledPixmap(const RectF&, const QPixmap&, const PointF&)
{
}
#endif

} // namespace mscz
