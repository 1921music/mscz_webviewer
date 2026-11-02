// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "hostio.h"
#include "mscz_engine.h"
#include "svgpaintprovider.h"

using namespace muse;
using namespace muse::draw;

static void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

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

        const std::string paths = render(mscz::SvgPaintProvider::SymbolMode::Path);
        require(paths.find("<use ") != std::string::npos, "inline symbols were not drawn");
        require(paths.find("Allegro ") != std::string::npos && paths.find(" fine") != std::string::npos,
                "words around an inline symbol were lost");
        require(paths.find("sus</text>") != std::string::npos, "legacy sus ligature was not replaced");
        for (char32_t cp : { 0xE048, 0xE18F, 0xE18A, 0xE10C, 0xE185 }) {
            require(paths.find(String::fromUcs4(cp).toStdString()) == std::string::npos,
                    "a private-use music character remains in path mode");
        }
        const std::string fonts = render(mscz::SvgPaintProvider::SymbolMode::Font);
        require(fonts.find(String::fromUcs4(0xE18F).toStdString()) != std::string::npos,
                "explicit font mode stopped emitting symbols as text");
        std::cout << "PASS mixed text, music fallback, legacy chords, font mode, single registration\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
