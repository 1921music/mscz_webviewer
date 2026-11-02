/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#pragma once

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "draw/ipaintprovider.h"

#include "fontsubset.h"
#include "glyphoutlines.h"

namespace mscz {

//! Writes what the engine paints as an SVG document.
//!
//! Painter reduces every drawing call to five provider primitives --
//! drawPath, drawPolygon, drawText, drawSymbol, drawPixmap -- so there is less
//! here than the breadth of painter.cpp suggests: drawLine, drawRect,
//! drawEllipse, drawRoundedRect, drawPolyline, drawArc and fillRect all arrive
//! as paths or polygons.
//!
//! Coordinates need no conversion, but not for the reason it first looks.
//! ScoreRenderer sets the painter's window to the page in engraving::DPI units
//! (1200 per inch) and the viewport to the page in device pixels, and
//! Painter::setWindow folds that mapping into the transform it hands a
//! provider -- passing the window along only "for debug purpose". So what
//! arrives here is already device space, and the viewBox is the *viewport*.
//!
//! Font sizes are scaled by DPI_F (5), matching the font engine: a point size
//! of P covers P * 5 units before the transform, which the view transform then
//! maps to P/72 inch on the device.
//!
//! Path mode also extracts SMuFL symbols from text runs (tempo marks, chord
//! names, etc.), leaving ordinary words selectable.
class SvgPaintProvider : public muse::draw::IPaintProvider
{
public:
    //! How drawSymbol is written out.
    enum class SymbolMode {
        //! <text> in the music font family. Smallest markup, and the text
        //! stays text, but the reader has to have the font.
        Font,
        //! The glyph outline once in <defs>, referenced by <use>. Needs no
        //! font at the far end, and matches the engine's own rendering
        //! exactly, since the outline comes from the same file the metrics do.
        Path,
    };

    //! Whether the fonts the page uses are embedded in the document.
    enum class FontMode {
        //! Name the families and leave it to the reader. Smallest, and right
        //! when the host page already declares them.
        Reference,
        //! Embed a subset of each family, cut to the code points this page
        //! uses, as a CSS @font-face data URI. Makes the document stand alone.
        EmbedSubset,
    };

    explicit SvgPaintProvider(SymbolMode symbolMode = SymbolMode::Font,
                              FontMode fontMode = FontMode::Reference);
    ~SvgPaintProvider() override;

    //! The SVG written so far. Valid once endTarget() has run.
    std::string svg() const;

    // IPaintProvider
    bool isActive() const override;
    void beginTarget(const std::string& name) override;
    void beforeEndTargetHook(muse::draw::Painter* painter) override;
    bool endTarget(bool endDraw = false) override;
    void beginObject(const std::string& name) override;
    void endObject() override;

    void setAntialiasing(bool arg) override;
    void setCompositionMode(muse::draw::CompositionMode mode) override;
    void setWindow(const muse::RectF& window) override;
    void setViewport(const muse::RectF& viewport) override;

    void setFont(const muse::draw::Font& font) override;
    const muse::draw::Font& font() const override;

    void setPen(const muse::draw::Pen& pen) override;
    void setNoPen() override;
    const muse::draw::Pen& pen() const override;

    void setBrush(const muse::draw::Brush& brush) override;
    const muse::draw::Brush& brush() const override;

    void save() override;
    void restore() override;

    double deviceLogicalDpi() const override;

    void setTransform(const muse::draw::Transform& transform) override;
    const muse::draw::Transform& transform() const override;

    void drawPath(const muse::draw::PainterPath& path) override;
    void drawPolygon(const muse::PointF* points, size_t pointCount, muse::draw::PolygonMode mode) override;

    void drawText(const muse::PointF& point, const muse::String& text) override;
    void drawText(const muse::RectF& rect, int flags, const muse::String& text) override;
    void drawTextWorkaround(const muse::draw::Font& f, const muse::PointF& pos, const muse::String& text) override;

    void drawSymbol(const muse::PointF& point, char32_t ucs4Code) override;

    void drawPixmap(const muse::PointF& point, const muse::draw::Pixmap& pm) override;
    void drawTiledPixmap(const muse::RectF& rect, const muse::draw::Pixmap& pm,
                         const muse::PointF& offset = muse::PointF()) override;

#ifndef NO_QT_SUPPORT
    void drawPixmap(const muse::PointF& point, const QPixmap& pm) override;
    void drawTiledPixmap(const muse::RectF& rect, const QPixmap& pm,
                         const muse::PointF& offset = muse::PointF()) override;
#endif

    bool hasClipping() const override;

    void setClipRect(const muse::RectF& rect) override;
    void setMask(const muse::RectF& background, const std::vector<muse::RectF>& maskRects) override;
    void setClipping(bool enable) override;

private:
    struct State {
        muse::draw::Pen pen;
        muse::draw::Brush brush;
        muse::draw::Font font;
        muse::draw::Transform transform;
        bool antialiasing = true;
        bool clipping = false;
        muse::RectF clipRect;
        //! Transform in force when clipRect was set. setClipRect takes logical
        //! coordinates, so the clip has to carry the transform of its own
        //! moment rather than whatever is current when a shape uses it.
        muse::draw::Transform clipTransform;
        //! Id of the <clipPath> in <defs> for clipRect, empty when none.
        std::string clipId;
    };

    //! The stroke/fill/transform/clip attributes for the current state.
    //! `filled` says whether the primitive takes the brush as a fill: a path
    //! or polygon does, a polyline does not.
    std::string paintAttributes(bool filled);
    //! Attributes for a <text>: font, the pen colour as fill, transform, clip.
    std::string textAttributes();
    std::string transformAttribute() const;
    std::string clipAttribute();

    //! Opens a <g> carrying `attrs` unless one with exactly those attributes
    //! is already open, so that runs of shapes sharing a transform, pen and
    //! clip -- which is most of a page -- state them once instead of each
    //! repeating five attributes.
    void openStateGroup(const std::string& attrs);
    void closeStateGroup();
    //! Registers a <clipPath> for the current clip rect if needed, returning
    //! its id.
    std::string ensureClipPath();

    //! Emits a symbol as a <use> of a glyph path in <defs>, adding the path on
    //! first sight. False if the glyph could not be read, so the caller can
    //! fall back to <text>.
    bool drawSymbolAsPath(const muse::PointF& point, char32_t ucs4Code);

    //! Notes that this page needs these code points of the current font, so
    //! endTarget can subset it.
    void noteUsedGlyphs(const muse::String& text);
    //! The <style> block with an @font-face per family used, each carrying its
    //! subset. Empty in Reference mode.
    std::string fontFaceStyle();

    static std::string colorToHex(const muse::Color& color);
    static std::string escapeText(const muse::String& text);
    static std::string formatNumber(double value);

    const SymbolMode m_symbolMode;
    const FontMode m_fontMode;

    bool m_active = false;
    std::string m_name;
    muse::RectF m_window;
    muse::RectF m_viewport;

    State m_state;
    std::vector<State> m_stack;

    //! Body, held separately because the <svg> header needs the viewBox, which
    //! only arrives via setWindow once painting has started.
    std::ostringstream m_body;
    std::ostringstream m_defs;
    int m_nextClipId = 0;
    GlyphOutlines m_glyphs;
    FontSubset m_subsetter;
    //! Code points drawn per face, collected while painting.
    std::map<FontSubset::FaceKey, std::set<char32_t> > m_usedGlyphs;
    //! Glyph id in <defs> by font family and code point, so each glyph is
    //! defined once per document.
    std::map<std::pair<FontSubset::FaceKey, char32_t>, std::string> m_glyphIds;
    int m_openObjectGroups = 0;
    bool m_stateGroupOpen = false;
    std::string m_stateGroupAttrs;

    std::string m_svg;
};

} // namespace mscz
