/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "draw/ifontprovider.h"
#include "draw/types/font.h"

namespace mscz {

//! QFontProvider's conventions, on FreeType instead of Qt.
//!
//! The engine's layout was written against QFontProvider, so matching its
//! answers is what keeps a page looking the same. Those answers are not just
//! "the font's metrics": QFontProvider measures through a QPaintDevice
//! reporting 1200 dpi, and carries four decisions of its own, each recorded in
//! its comments and each reproduced here:
//!
//!  - inFont avoids QFontMetricsF::inFontUcs4, which consults Qt's fallbacks
//!    even with noFontMerging set. It asks for a glyph index and then checks
//!    the glyph has a valid bounding box, because some symbols exist in a font
//!    without having a glyph -- U+EE80 in Bravura among them.
//!  - boundingRect for a music symbol does not use font metrics at all, since
//!    those come from rasterisation and are not accurate enough. Qt builds a
//!    QPainterPath at four times the size and scales the vector box back down.
//!    Here the outline is read directly in font units, so no upscaling is
//!    needed and the result is exact.
//!  - tightBoundingRect returns empty for an invalid rect, which Qt sometimes
//!    produced and which corrupted layout.
//!  - The single-character overloads defer to the string ones for anything
//!    needing surrogates.
//!
//! Upstream's FontProvider, over FontsEngine, has none of these and converts
//! point size with muse::draw::DPI (360) rather than 1200, which is why
//! swapping to it moved almost every element on the page.
class CompatFontProvider : public muse::draw::IFontProvider
{
public:
    CompatFontProvider();
    ~CompatFontProvider() override;

    int addSymbolFont(const muse::String& family, const muse::io::path_t& path) override;

    double lineSpacing(const muse::draw::Font& f) const override;
    double xHeight(const muse::draw::Font& f) const override;
    double height(const muse::draw::Font& f) const override;
    double capHeight(const muse::draw::Font& f) const override;
    double ascent(const muse::draw::Font& f) const override;
    double descent(const muse::draw::Font& f) const override;

    bool inFont(const muse::draw::Font& f, char32_t ucs4) const override;

    double horizontalAdvance(const muse::draw::Font& f, const muse::String& string) const override;
    double horizontalAdvance(const muse::draw::Font& f, char32_t ucs4) const override;

    muse::RectF boundingRect(const muse::draw::Font& f, const muse::String& string) const override;
    muse::RectF boundingRect(const muse::draw::Font& f, char32_t ucs4) const override;

    muse::RectF tightBoundingRect(const muse::draw::Font& f, const muse::String& string) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    CompatFontProvider(const CompatFontProvider&) = delete;
    CompatFontProvider& operator=(const CompatFontProvider&) = delete;
};

} // namespace mscz
