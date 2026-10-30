/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "draw/types/font.h"

namespace mscz {

//! Cuts a font down to the code points a page actually uses, so the subset can
//! be embedded in the SVG and the document stands alone.
//!
//! Worth doing because the used set is tiny: a two-page choral score touches 15
//! glyphs of the music font and about 40 of its text font, out of the 493 in
//! Leland.
//!
//! The work is HarfBuzz's hb-subset, which is the same code fontTools and
//! Chrome's font servers use, rather than anything hand-rolled: OTF/CFF
//! subsetting means rewriting charstrings, cmap, hmtx and the layout tables,
//! and getting it subtly wrong shows up as a missing or malformed glyph.
class FontSubset
{
public:
    FontSubset();
    ~FontSubset();

    //! Identifies one face: family plus the bold and italic flags, since a
    //! family's faces are separate files and need separate @font-face rules.
    struct FaceKey {
        std::string family;
        bool bold = false;
        bool italic = false;
        muse::draw::Font::Type type = muse::draw::Font::Type::Undefined;

        bool operator<(const FaceKey& o) const
        {
            if (family != o.family) { return family < o.family; }
            if (bold != o.bold) { return bold < o.bold; }
            if (italic != o.italic) { return italic < o.italic; }
            return type < o.type;
        }
    };

    static FaceKey keyFor(const muse::draw::Font& font);

    //! The subset as a font file, empty if the face could not be found or
    //! subsetted.
    std::vector<std::uint8_t> subset(const FaceKey& key, const std::set<char32_t>& codepoints);

    //! "opentype" or "truetype", for the @font-face src format() hint.
    static std::string formatHint(const std::vector<std::uint8_t>& font);

    static std::string base64(const std::vector<std::uint8_t>& bytes);

private:
    struct Impl;
    Impl* m_impl = nullptr;

    FontSubset(const FontSubset&) = delete;
    FontSubset& operator=(const FontSubset&) = delete;
};

} // namespace mscz
