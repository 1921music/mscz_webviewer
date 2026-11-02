// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <limits>
#include <stdexcept>
#include "hostio.h"
#include "mscz_engine.h"
#include "displaytranspose.h"

static void require(bool ok, const std::string& message)
{
    if (!ok) { throw std::runtime_error(message); }
}
static std::vector<std::string> fingerprint(mscz::Session& score)
{
    std::vector<std::string> result;
    std::string error;
    require(score.layoutFingerprint(result, error), error);
    return result;
}
static std::string svg(mscz::Session& score)
{
    std::string result, error;
    for (int page = 0; page < score.pageCount(); ++page) {
        std::string current;
        require(score.renderPageSvg(page, 300, mscz::Session::SvgSymbols::Path,
                                    mscz::Session::SvgFonts::EmbedSubset, current, error), error);
        result += current;
    }
    return result;
}
int main(int argc, char** argv)
{
    try {
        using namespace mu::engraving;
        using namespace mscz::spelling;
        for (int k = -40; k <= 40; ++k) {
            const int normalized = readableKey(k);
            require(normalized >= -6 && normalized <= 5 && (normalized - k) % 12 == 0,
                    "key policy changed pitch or exceeded six accidentals");
        }
        require(readableKey(6) == -6, "F sharp key must prefer G flat");
        Event third;
        third.pitch = 67; third.original = TPC_F_SS; third.key = 5; third.harmonic = TPC_F_SS;
        require(optimize({third}).tpcs[0] == TPC_F_SS, "D sharp 7 third lost harmonic spelling");
        third.approach = true;
        require(optimize({third}).tpcs[0] == TPC_G, "approach retained a double sharp");
        third.original = TPC_D_BB; third.pitch = 60; third.key = -5;
        third.harmonic = TPC_D_BB;
        require(optimize({third}).tpcs[0] == TPC_C, "approach retained a double flat");
        Event neutral;
        neutral.original = TPC_E_SS; neutral.pitch = 66;
        require(optimize({neutral}).tpcs[0] == TPC_F_S, "neutral F sharp preference missing");
        neutral.original = TPC_G_SSS; neutral.pitch = 70;
        require(optimize({neutral}).tpcs[0] == TPC_B_B, "neutral B flat preference missing");
        Event minorThird;
        minorThird.original = TPC_E_B; minorThird.pitch = 63; minorThird.harmonic = TPC_E_B;
        require(optimize({minorThird}).tpcs[0] == TPC_E_B, "original blue-note spelling lost");
        require(argc > 1, "pass one or more .mscz sample paths");
        mscz::Engine::setResourceProvider(mscz::makeCachingProvider(
            mscz::makeDirectoryProvider(MSCZ_RESOURCE_ROOT, MSCZ_RESOURCE_MANIFEST)));
        for (int i = 1; i < argc; ++i) {
            std::string error;
            auto score = mscz::Session::openPath(argv[i], error);
            require(bool(score), error);
            const auto original = fingerprint(*score);
            const auto originalSvg = svg(*score);
            const int originalPages = score->pageCount();
            require(score->viewTransposeSemitones() == 0, "new score is not at original pitch");
            for (auto mode : { mscz::Session::TransposeSpelling::Preserve, mscz::Session::TransposeSpelling::Readable }) {
            require(score->setViewTranspose(0, mode, error), error);
            require(svg(*score) == originalSvg, "mode switch at zero changed source notation");
            for (int offset : { 1, 2, -1, -2, 6, -6, 12, -12, 0 }) {
                require(score->setViewTransposeSemitones(offset, error), error);
                require(score->viewTransposeSemitones() == offset, "wrong transposition offset");
                const auto currentSvg = svg(*score);
                require(offset == 0 ? currentSvg == originalSvg : currentSvg != originalSvg,
                        std::string(argv[i]) + ": transposition/reset did not affect SVG at offset " + std::to_string(offset));
            }
            require(fingerprint(*score) == original, "reset changed original layout");
            require(score->pageCount() == originalPages, "reset changed page count");
            require(score->setViewTransposeSemitones(2, error), error);
            const auto plusTwo = svg(*score);
            const auto otherMode = mode == mscz::Session::TransposeSpelling::Preserve
                                 ? mscz::Session::TransposeSpelling::Readable : mscz::Session::TransposeSpelling::Preserve;
            require(score->setViewTranspose(2, otherMode, error), error);
            require(score->setViewTranspose(2, mode, error), error);
            require(svg(*score) == plusTwo, "mode roundtrip accumulated spelling changes");
            require(score->setViewTransposeSemitones(1, error), error);
            require(score->setViewTransposeSemitones(2, error), error);
            require(svg(*score) == plusTwo, "absolute transposition depends on prior steps");
            for (int invalid : { -13, 13, std::numeric_limits<int>::max(), std::numeric_limits<int>::min() }) {
                require(!score->setViewTransposeSemitones(invalid, error) && !error.empty(), "invalid interval accepted");
                require(score->viewTransposeSemitones() == 2 && svg(*score) == plusTwo, "failure changed the view");
            }
            require(score->setViewTransposeSemitones(0, error), error);
            require(fingerprint(*score) == original && svg(*score) == originalSvg, "final reset is not exact");
            require(score->viewTransposeSpelling() == mode, "reset lost chosen spelling mode");
            require(!score->setViewTranspose(1, mscz::Session::TransposeSpelling(7), error), "invalid spelling mode accepted");
            require(svg(*score) == originalSvg && score->viewTransposeSpelling() == mode, "invalid mode changed the view");
            }
            std::cout << "PASS transpose, absolute offsets, limits, exact reset: " << argv[i] << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
