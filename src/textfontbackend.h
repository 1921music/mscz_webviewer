// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <memory>
#include "draw/ifontprovider.h"

namespace mscz {
// All distances are device pixels at 1200 dpi, relative to a text baseline.
// The backend also supplies the SVG font stack: layout and drawing must agree.
struct TextMetrics {
    double advance = 0, ascent = 0, descent = 0, lineSpacing = 0, xHeight = 0, capHeight = 0;
    muse::RectF ink;
};
class TextFontBackend {
public:
    virtual ~TextFontBackend() = default;
    virtual bool handles(const muse::draw::Font&) const = 0;
    virtual TextMetrics measure(const muse::draw::Font&, const muse::String&) const = 0;
    virtual muse::String svgFamily(const muse::draw::Font&) const = 0;
};
// Configure before the first score is opened. Null selects embedded/native
// metrics. MuseScore only ever sees its original IFontProvider interface.
void setTextFontBackend(std::shared_ptr<TextFontBackend> backend);
std::shared_ptr<TextFontBackend> textFontBackend(const muse::draw::Font& font);
bool containsMusicCharacters(const muse::String& text);
std::shared_ptr<muse::draw::IFontProvider> routeTextFonts(std::shared_ptr<muse::draw::IFontProvider> embedded);
}
