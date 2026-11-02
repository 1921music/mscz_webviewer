/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#pragma once

#include <map>
#include <string>

#include "draw/types/font.h"

namespace mscz {

//! Turns a glyph into an SVG path, so symbols can be drawn without the
//! reader needing the music font.
//!
//! Paths come out in font units with y negated, not scaled to any particular
//! size, so one path serves every occurrence of that glyph at any size: the
//! caller scales by fontSize/unitsPerEm on the <use>. Hence `unitsPerEm` in
//! the result.
class GlyphOutlines
{
public:
    GlyphOutlines();
    ~GlyphOutlines();

    struct Glyph {
        bool valid = false;
        //! An SVG path's `d`, in font units, y already flipped for SVG.
        std::string path;
        int unitsPerEm = 1000;
    };

    //! Looks the glyph up by the font the painter is using. Results are cached
    //! per face, so repeated calls cost a map lookup.
    const Glyph& glyph(const muse::draw::Font& font, char32_t ucs4);

    //! Shaped horizontal advance in em units, for positioning text after a
    //! symbol removed from a mixed text run. Uses the same resolved face.
    double advanceEm(const muse::draw::Font& font, const muse::String& text);

private:
    struct Face;

    Face* faceFor(const muse::draw::Font& font);

    struct Impl;
    Impl* m_impl = nullptr;

    GlyphOutlines(const GlyphOutlines&) = delete;
    GlyphOutlines& operator=(const GlyphOutlines&) = delete;
};

} // namespace mscz
