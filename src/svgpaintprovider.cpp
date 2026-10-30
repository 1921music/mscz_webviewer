/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "svgpaintprovider.h"

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

} // namespace

namespace mscz {

SvgPaintProvider::SvgPaintProvider() = default;
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
    m_openGroups = 0;
    m_svg.clear();
}

void SvgPaintProvider::beforeEndTargetHook(Painter*)
{
}

bool SvgPaintProvider::endTarget(bool)
{
    while (m_openGroups > 0) {
        m_body << "</g>\n";
        --m_openGroups;
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
    m_body << "<g data-object=\"" << name << "\">\n";
    ++m_openGroups;
}

void SvgPaintProvider::endObject()
{
    if (m_openGroups > 0) {
        m_body << "</g>\n";
        --m_openGroups;
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

void SvgPaintProvider::writeClipAttribute()
{
    const std::string id = ensureClipPath();
    if (!id.empty()) {
        m_body << " clip-path=\"url(#" << id << ")\"";
    }
}

void SvgPaintProvider::writeTransformAttribute()
{
    const Transform& t = m_state.transform;
    // Transform::type() is private, so test the matrix directly.
    if (t.m11() == 1.0 && t.m12() == 0.0 && t.m21() == 0.0 && t.m22() == 1.0
        && t.dx() == 0.0 && t.dy() == 0.0) {
        return;
    }
    m_body << " transform=\"matrix("
           << formatNumber(t.m11()) << " " << formatNumber(t.m12()) << " "
           << formatNumber(t.m21()) << " " << formatNumber(t.m22()) << " "
           << formatNumber(t.dx()) << " " << formatNumber(t.dy()) << ")\"";
}

void SvgPaintProvider::writeCommonAttributes(bool filled)
{
    const Pen& p = m_state.pen;
    const Brush& b = m_state.brush;

    if (filled && b.style() != BrushStyle::NoBrush) {
        m_body << " fill=\"" << colorToHex(b.color()) << "\"";
        if (b.color().alpha() != 255) {
            m_body << " fill-opacity=\"" << formatNumber(b.color().alpha() / 255.0) << "\"";
        }
    } else {
        m_body << " fill=\"none\"";
    }

    if (p.style() == PenStyle::NoPen) {
        m_body << " stroke=\"none\"";
    } else {
        m_body << " stroke=\"" << colorToHex(p.color()) << "\""
               << " stroke-width=\"" << formatNumber(p.widthF()) << "\"";
        if (p.color().alpha() != 255) {
            m_body << " stroke-opacity=\"" << formatNumber(p.color().alpha() / 255.0) << "\"";
        }
        if (p.capStyle() != PenCapStyle::SquareCap) {
            m_body << " stroke-linecap=\"" << lineCapName(p.capStyle()) << "\"";
        }
        if (p.joinStyle() != PenJoinStyle::BevelJoin) {
            m_body << " stroke-linejoin=\"" << lineJoinName(p.joinStyle()) << "\"";
        }
        if (p.style() != PenStyle::SolidLine) {
            // Pen::dashPattern() already turns the named styles into numbers,
            // in units of the pen width, which is what SVG wants scaled by it.
            const std::vector<double> dashes = p.dashPattern();
            if (!dashes.empty()) {
                m_body << " stroke-dasharray=\"";
                for (size_t i = 0; i < dashes.size(); ++i) {
                    if (i) {
                        m_body << ",";
                    }
                    m_body << formatNumber(dashes[i] * p.widthF());
                }
                m_body << "\"";
            }
        }
    }

    if (!m_state.antialiasing) {
        m_body << " shape-rendering=\"crispEdges\"";
    }

    writeTransformAttribute();
    writeClipAttribute();
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

    m_body << "<path d=\"" << d.str() << "\"";
    if (path.fillRule() == PainterPath::FillRule::OddEvenFill) {
        m_body << " fill-rule=\"evenodd\"";
    }
    writeCommonAttributes(true);
    m_body << "/>\n";
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
    m_body << (polyline ? "<polyline points=\"" : "<polygon points=\"") << pts.str() << "\"";
    if (mode == PolygonMode::OddEven) {
        m_body << " fill-rule=\"evenodd\"";
    }
    writeCommonAttributes(!polyline);
    m_body << "/>\n";
}

void SvgPaintProvider::drawText(const PointF& point, const String& text)
{
    if (text.empty()) {
        return;
    }

    // Qt places the baseline at the given point, and so does SVG <text> by
    // default, so the position carries over unchanged.
    m_body << "<text x=\"" << formatNumber(point.x()) << "\""
           << " y=\"" << formatNumber(point.y()) << "\""
           << " font-family=\"" << escapeText(m_state.font.family().id()) << "\""
           << " font-size=\"" << formatNumber(m_state.font.pointSizeF() * kFontSizeScale) << "\"";
    if (m_state.font.bold()) {
        m_body << " font-weight=\"bold\"";
    }
    if (m_state.font.italic()) {
        m_body << " font-style=\"italic\"";
    }

    // Text takes the pen colour as its fill, the way QPainter draws it.
    const Pen& p = m_state.pen;
    m_body << " fill=\"" << colorToHex(p.style() == PenStyle::NoPen ? m_state.brush.color() : p.color()) << "\"";
    writeTransformAttribute();
    writeClipAttribute();
    m_body << ">" << escapeText(text) << "</text>\n";
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

void SvgPaintProvider::drawSymbol(const PointF& point, char32_t ucs4Code)
{
    // TODO Emit the glyph outline into <defs> and reference it with <use>, so
    // the SVG does not need the music font. Until then this is a <text> with
    // the music font family, like QPainterProvider does.
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
