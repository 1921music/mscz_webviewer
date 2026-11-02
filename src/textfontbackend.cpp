// SPDX-License-Identifier: GPL-3.0-only
#include "textfontbackend.h"
#include <algorithm>
namespace mscz {
namespace {
std::shared_ptr<TextFontBackend> backend;
using muse::draw::Font;
using muse::String;
using muse::RectF;
class RoutedFonts final : public muse::draw::IFontProvider {
public:
    explicit RoutedFonts(std::shared_ptr<IFontProvider> value) : embedded(std::move(value)) {}
    int addSymbolFont(const String& name, const muse::io::path_t& path) override { return embedded->addSymbolFont(name, path); }
#define METRIC(method, member) \
    double method(const Font& f) const override { \
        auto b = textFontBackend(f); return b ? b->measure(f, {}).member : embedded->method(f); }
    METRIC(lineSpacing, lineSpacing)
    METRIC(xHeight, xHeight)
    METRIC(capHeight, capHeight)
    METRIC(ascent, ascent)
    METRIC(descent, descent)
#undef METRIC
    double height(const Font& f) const override {
        auto b = textFontBackend(f);
        if (!b) { return embedded->height(f); }
        const auto m = b->measure(f, {}); return m.ascent + m.descent;
    }
    bool inFont(const Font& f, char32_t cp) const override {
        const auto text = String::fromUcs4(cp);
        auto b = textFontBackend(f);
        return b && !containsMusicCharacters(text) ? !b->measure(f, text).ink.isEmpty() : embedded->inFont(f, cp);
    }
    double horizontalAdvance(const Font& f, const String& text) const override {
        auto b = textFontBackend(f);
        return b && !containsMusicCharacters(text) ? b->measure(f, text).advance : embedded->horizontalAdvance(f, text);
    }
    double horizontalAdvance(const Font& f, char32_t cp) const override { return horizontalAdvance(f, String::fromUcs4(cp)); }
    RectF boundingRect(const Font& f, const String& text) const override {
        auto b = textFontBackend(f);
        if (!b || containsMusicCharacters(text)) { return embedded->boundingRect(f, text); }
        if (text.empty()) { return {}; }
        const auto m = b->measure(f, text);
        const double left = std::min(0.0, m.ink.left());
        return RectF(left, -m.ascent, std::max(m.advance, m.ink.right()) - left, m.ascent + m.descent);
    }
    RectF boundingRect(const Font& f, char32_t cp) const override {
        auto b = textFontBackend(f);
        const auto text = String::fromUcs4(cp);
        return b && !containsMusicCharacters(text) ? b->measure(f, text).ink : embedded->boundingRect(f, cp);
    }
    RectF tightBoundingRect(const Font& f, const String& text) const override {
        auto b = textFontBackend(f);
        return b && !containsMusicCharacters(text) ? b->measure(f, text).ink : embedded->tightBoundingRect(f, text);
    }
private:
    std::shared_ptr<IFontProvider> embedded;
};
}
void setTextFontBackend(std::shared_ptr<TextFontBackend> value) { backend = std::move(value); }
std::shared_ptr<TextFontBackend> textFontBackend(const muse::draw::Font& font) {
    using Type = muse::draw::Font::Type;
    if (font.type() == Type::MusicSymbol || font.type() == Type::MusicSymbolText
        || font.type() == Type::Icon || font.type() == Type::Tablature) { return {}; }
    if (font.family() == Font::FontFamily(u"Campania") || font.family() == Font::FontFamily(u"MscoreBC")) { return {}; }
    return backend && backend->handles(font) ? backend : nullptr;
}
bool containsMusicCharacters(const muse::String& text) {
    for (char32_t cp : text.toStdU32String()) {
        if ((cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0x1D100 && cp <= 0x1D1FF)) { return true; }
    }
    return false;
}
std::shared_ptr<muse::draw::IFontProvider> routeTextFonts(std::shared_ptr<muse::draw::IFontProvider> embedded) {
    return std::make_shared<RoutedFonts>(std::move(embedded));
}
}
