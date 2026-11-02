// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "hostio.h"
#include "mscz_engine.h"
#include "svgpaintprovider.h"
#include "textfontbackend.h"
#include "global/modularity/ioc.h"
#include "draw/internal/ifontsdatabase.h"
#include "engraving/iengravingfontsprovider.h"
#include "engraving/iengravingfont.h"

using namespace muse;
using namespace muse::draw;

static void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TestTextBackend : public mscz::TextFontBackend {
public:
    bool handles(const Font&) const override { return true; }
    mscz::TextMetrics measure(const Font&, const String&) const override {
        return {123, 90, 20, 115, 42, 65, RectF(-2, -80, 125, 95)};
    }
    String svgFamily(const Font&) const override { return u"Host Serif, serif"; }
};

static std::string render(mscz::SvgPaintProvider::SymbolMode mode)
{
    mscz::SvgPaintProvider painter(mode, mscz::SvgPaintProvider::FontMode::EmbedSubset);
    painter.beginTarget("mixed text regression");
    painter.setViewport(RectF(0, 0, 1000, 1000));
    Font text(u"Edwin", Font::Type::Text);
    text.setPointSizeF(12);
    painter.setFont(text);
    // A symbol embedded in an ordinary text run, with words on both sides.
    painter.drawText(PointF(10, 30), u"Allegro \uE048 fine");
    Font jazz(u"MuseJazz Text", Font::Type::Harmony);
    jazz.setPointSizeF(12);
    painter.setFont(jazz);
    // Legacy half-diminished/major/sharp and the "sus" word ligature.
    painter.drawText(PointF(10, 80), u"C\uE18F7 D\uE18A F\uE10C \uE185");
    painter.endTarget();
    return painter.svg();
}

int main()
{
    try {
        mscz::Engine::setResourceProvider(mscz::makeCachingProvider(
            mscz::makeDirectoryProvider(MSCZ_RESOURCE_ROOT, MSCZ_RESOURCE_MANIFEST)));
        std::ostringstream diagnostics;
        auto previous = std::cerr.rdbuf(diagnostics.rdbuf());
        mscz::Engine::initializeIfNeeded();
        mscz::Engine::initializeIfNeeded();
        std::cerr.rdbuf(previous);
        require(diagnostics.str().find("double register:") == std::string::npos, "duplicate service registration");
        auto fdb = modularity::globalIoc()->resolve<IFontsDatabase>("mscz_test");
        auto music = modularity::globalIoc()->resolve<mu::engraving::IEngravingFontsProvider>("mscz_test");
        require(bool(fdb) && bool(music), "font services missing");
        for (const auto* name : {u"MuseJazz Text", u"Petaluma Text", u"Petaluma Script", u"Inkpen2",
                                 u"Inkpen2 Script", u"Inkpen2 Chords", u"Reprise", u"Reprise Script", u"Reprise Chords"}) {
            FontDataKey key{Font::FontFamily(name)};
            require(fdb->fontPath(key, Font::Type::Harmony).toStdString() == ":/fonts/musejazz/MuseJazzText.otf",
                    "jazz text alias did not resolve to MuseJazz Text");
        }
        for (const auto* name : {"MuseJazz", "Petaluma", "Inkpen2", "Reprise", "Reprise Std"}) {
            require(music->fontByName(name)->family() == "MuseJazz", "jazz music alias fell back to Leland");
        }

        const std::string paths = render(mscz::SvgPaintProvider::SymbolMode::Path);
        require(paths.find("<use ") != std::string::npos, "inline symbols were not drawn");
        require(paths.find("Allegro ") != std::string::npos && paths.find(" fine") != std::string::npos,
                "words around an inline symbol were lost");
        require(paths.find("font-family=\"MuseJazz Text\"") != std::string::npos, "MuseJazz text was not embedded");
        for (char32_t cp : { 0xE048, 0xE18F, 0xE18A, 0xE10C, 0xE185 }) {
            require(paths.find(String::fromUcs4(cp).toStdString()) == std::string::npos,
                    "a private-use music character remains in path mode");
        }
        const std::string fonts = render(mscz::SvgPaintProvider::SymbolMode::Font);
        require(fonts.find(String::fromUcs4(0xE18F).toStdString()) != std::string::npos,
                "explicit font mode stopped emitting symbols as text");
        auto provider = modularity::globalIoc()->resolve<IFontProvider>("mscz_test");
        Font text(u"Times New Roman", Font::Type::Text); text.setPointSizeF(12);
        Font symbol(u"Leland", Font::Type::MusicSymbol); symbol.setPointSizeF(12);
        const auto originalAdvance = provider->horizontalAdvance(text, u"Hello");
        const auto symbolAdvance = provider->horizontalAdvance(symbol, char32_t(0xE050));
        mscz::setTextFontBackend(std::make_shared<TestTextBackend>());
        require(provider->horizontalAdvance(text, u"日本語 中文 한국어") == 123, "host text metrics did not reach MuseScore");
        require(provider->ascent(text) == 90 && provider->descent(text) == 20, "host vertical metrics lost");
        require(provider->horizontalAdvance(symbol, char32_t(0xE050)) == symbolAdvance, "host backend changed music metrics");
        require(render(mscz::SvgPaintProvider::SymbolMode::Path).find("Host Serif, serif") != std::string::npos,
                "SVG did not use the measuring backend's font family");
        mscz::setTextFontBackend(nullptr);
        require(provider->horizontalAdvance(text, u"Hello") == originalAdvance, "embedded font backend could not be restored");
        std::cout << "PASS mixed text, music fallback, legacy chords, font mode, single registration\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
