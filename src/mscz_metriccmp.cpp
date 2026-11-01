/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */

//! Checks CompatFontProvider against the QFontProvider it was ported from.
//!
//! Not a tuning loop: the port was written by reading QFontProvider, and this
//! says whether the reading was right. Matching on one score would not show
//! they agree in general, so it asks both for the same metrics directly.
//!
//! Only buildable with Qt, which is the one place both exist.

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "draw/types/font.h"
#include "draw/internal/qfontprovider.h"

#include "compatfontprovider.h"
#include "hostio.h"
#include "mscz_engine.h"

using namespace muse;
using namespace muse::draw;

namespace {

struct Probe {
    const char* family;
    Font::Type type;
    double pointSize;
    const char* text;
};

int g_checked = 0;
int g_off = 0;

void row(const char* what, double qt, double ours)
{
    ++g_checked;
    const double diff = std::fabs(qt - ours);
    const double scale = std::max(std::fabs(qt), 1.0);
    const bool off = (diff / scale) > 0.01;      // within 1% counts as agreeing
    if (off) {
        ++g_off;
    }
    std::printf("  %-20s %12.3f %12.3f %10.4f  %s\n",
                what, qt, ours, ours != 0.0 ? qt / ours : 0.0, off ? "<-- differs" : "");
}

void compare(const Probe& p, IFontProvider* qt, IFontProvider* ours)
{
    Font f(String::fromUtf8(p.family), p.type);
    f.setPointSizeF(p.pointSize);

    const String text = String::fromUtf8(p.text);

    std::printf("\n%s  %gpt  \"%s\"\n", p.family, p.pointSize, p.text);
    std::printf("  %-20s %12s %12s %10s\n", "", "Qt", "ours", "Qt/ours");

    row("ascent", qt->ascent(f), ours->ascent(f));
    row("descent", qt->descent(f), ours->descent(f));
    row("height", qt->height(f), ours->height(f));
    row("lineSpacing", qt->lineSpacing(f), ours->lineSpacing(f));
    row("xHeight", qt->xHeight(f), ours->xHeight(f));
    row("capHeight", qt->capHeight(f), ours->capHeight(f));
    row("horizontalAdvance", qt->horizontalAdvance(f, text), ours->horizontalAdvance(f, text));

    const RectF qtBox = qt->boundingRect(f, text);
    const RectF ourBox = ours->boundingRect(f, text);
    row("boundingRect.x", qtBox.left(), ourBox.left());
    row("boundingRect.y", qtBox.top(), ourBox.top());
    row("boundingRect.w", qtBox.width(), ourBox.width());
    row("boundingRect.h", qtBox.height(), ourBox.height());

    const RectF qtTight = qt->tightBoundingRect(f, text);
    const RectF ourTight = ours->tightBoundingRect(f, text);
    row("tightRect.w", qtTight.width(), ourTight.width());
    row("tightRect.h", qtTight.height(), ourTight.height());

    // Single code point, which is the path music symbols take.
    const std::u32string u32 = text.toStdU32String();
    if (!u32.empty()) {
        const char32_t cp = u32.front();
        row("advance(cp)", qt->horizontalAdvance(f, cp), ours->horizontalAdvance(f, cp));

        const RectF qtCp = qt->boundingRect(f, cp);
        const RectF ourCp = ours->boundingRect(f, cp);
        row("box(cp).x", qtCp.left(), ourCp.left());
        row("box(cp).y", qtCp.top(), ourCp.top());
        row("box(cp).w", qtCp.width(), ourCp.width());
        row("box(cp).h", qtCp.height(), ourCp.height());

        std::printf("  %-20s %12s %12s\n", "inFont(cp)",
                    qt->inFont(f, cp) ? "yes" : "no", ours->inFont(f, cp) ? "yes" : "no");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    const std::string resourceRoot = argc > 1 ? argv[1] : MSCZ_RESOURCE_ROOT;
    const std::string manifest = argc > 2 ? argv[2] : MSCZ_RESOURCE_MANIFEST;

    // The engine has to be up for the fonts database to know any fonts.
    mscz::Engine::setResourceProvider(
        mscz::makeCachingProvider(mscz::makeDirectoryProvider(resourceRoot, manifest)));
    mscz::Engine::initializeIfNeeded();

    QFontProvider qt;
    mscz::CompatFontProvider ours;

    const std::vector<Probe> probes = {
        { "Edwin", Font::Type::Text, 10.0, "Soprano" },
        { "Edwin", Font::Type::Text, 10.0, "I" },
        { "Edwin", Font::Type::Text, 24.0, "AMAZING GRACE" },
        { "Edwin", Font::Type::Text, 7.5, "wretch" },
        // The lyric syllables from the demo score, whose boxes came out about
        // 12% narrow. tightBoundingRect is what sets a lyric's bbox.
        { "Edwin", Font::Type::Text, 10.0, "A" },
        { "Edwin", Font::Type::Text, 10.0, "ma" },
        { "Edwin", Font::Type::Text, 10.0, "zing" },
        { "Edwin", Font::Type::Text, 10.0, "grace" },
        { "Edwin", Font::Type::Text, 10.0, "sound" },
        // The demo score asks for these two, and neither is shipped. Qt
        // resolves a name it does not have through fontconfig; the fonts
        // database falls back to its default. If they land on different fonts
        // the advances differ, and that is a substitution difference rather
        // than a metric one.
        { "Times New Roman", Font::Type::Text, 10.0, "zing" },
        { "Times New Roman", Font::Type::Text, 10.0, "sound" },
        { "FreeSerif", Font::Type::Text, 10.0, "7" },
        { "Leland", Font::Type::MusicSymbol, 10.0, "\xEE\x82\xA4" },   // U+E0A4 notehead
        { "Leland", Font::Type::MusicSymbol, 20.0, "\xEE\x81\x90" },   // U+E050 treble clef
        // U+E261 accidentalNatural, not the notehead this used to ask for.
        // Leland Text carries only the glyphs that make sense inline -- 21
        // ranges, no noteheads -- so asking it for U+E0A4 made every row of
        // this probe disagree: we answered "no such glyph" and Qt quietly
        // measured a fallback font, which is the behaviour QFontProvider's
        // inFont was written to avoid in the first place. Our answer was the
        // right one and the probe was wrong.
        { "Leland Text", Font::Type::MusicSymbolText, 10.0, "\xEE\x89\xA1" },
    };

    for (const Probe& p : probes) {
        compare(p, &qt, &ours);
    }

    std::printf("\n%d of %d metrics differ by more than 1%%\n", g_off, g_checked);
    return g_off == 0 ? 0 : 1;
}
