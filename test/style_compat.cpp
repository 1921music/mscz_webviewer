// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <stdexcept>
#include "mscz_engine.h"
#include "global/modularity/ioc.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/compat/engravingcompat.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/box.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/rw/mscloader.h"
using namespace mu::engraving;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::unique_ptr<MasterScore> fixture() {
    std::unique_ptr<MasterScore> score(compat::ScoreAccess::createMasterScoreWithDefaultStyle(muse::modularity::globalCtx()));
    score->setFileInfoProvider(mscz::makeStreamFileInfoProvider({"browser_text.mscx"}));
    MscReader::Params params;
    params.filePath = MSCZ_STYLE_COMPAT_FIXTURE;
    params.mainFileName = u"browser_text.mscx";
    params.mode = MscIoMode::Dir;
    MscReader reader(params);
    require(bool(reader.open()), "fixture could not open");
    MscLoader loader;
    require(bool(loader.loadMscz(score.get(), reader, nullptr, false)), "fixture could not load");
    require(score->first() && score->first()->isVBox(), "fixture title frame missing");
    return score;
}
int main(int argc, char** argv) {
    try {
        mscz::Engine::setResourceProvider(mscz::makeCachingProvider(
            mscz::makeDirectoryProvider(MSCZ_RESOURCE_ROOT, MSCZ_RESOURCE_MANIFEST)));
        mscz::Engine::initializeIfNeeded();
        for (int version : {450, 460, 470}) {
            auto score = fixture(); score->setMscVersion(version);
            compat::EngravingCompat::doPreLayoutCompatIfNeeded(score.get());
            auto box = static_cast<VBox*>(score->first());
            const double expected = version < 460 ? 0 : 2;
            require(box->paddingToNotationAbove().val() == expected
                    && box->paddingToNotationBelow().val() == expected, "legacy/modern frame compatibility changed");
        }
        auto explicitScore = fixture(); explicitScore->setMscVersion(460);
        auto explicitBox = static_cast<VBox*>(explicitScore->first());
        explicitBox->setProperty(Pid::PADDING_TO_NOTATION_BELOW, Spatium(3.0));
        explicitBox->setPropertyFlags(Pid::PADDING_TO_NOTATION_BELOW, PropertyFlags::UNSTYLED);
        compat::EngravingCompat::doPreLayoutCompatIfNeeded(explicitScore.get());
        require(explicitBox->paddingToNotationBelow().val() == 3, "modern explicit padding was overwritten");
        if (argc > 1) {
            std::string error;
            auto score = mscz::Session::openPath(argv[1], error);
            require(bool(score), error.c_str());
            require(score->pageCount() == 1, "Shade Of The Cedar Tree must fit on one page");
        }
        std::cout << "PASS standard pre-layout compatibility, modern frame padding and pagination\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
