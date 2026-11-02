// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include "mscz_engine.h"
#include "engraving/engravingproject.h"
#include "global/modularity/ioc.h"
#include "global/io/buffer.h"
#include "global/serialization/zipwriter.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/compat/engravingcompat.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/box.h"
#include "engraving/dom/page.h"
#include "engraving/dom/staff.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/rw/mscloader.h"
#include "engraving/style/style.h"
#include "engraving/style/defaultstyle.h"
using namespace mu::engraving;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class MemoryScore : public mscz::IReadStream {
public:
    explicit MemoryScore(const muse::ByteArray& data) : m_data(data) {}
    uint64_t size() const override { return m_data.size(); }
    size_t read(uint8_t* dest, uint64_t pos, size_t len) override {
        if (pos >= size()) return 0;
        len = std::min(len, size_t(size() - pos));
        std::memcpy(dest, m_data.constData() + pos, len);
        return len;
    }
private:
    muse::ByteArray m_data;
};
static void checkDesktopLoadParity() {
    // An extreme saved spacing value exposes the stale constructor-time table.
    std::ifstream input(MSCZ_STYLE_COMPAT_FIXTURE);
    std::string xml((std::istreambuf_iterator<char>(input)), {});
    require(!xml.empty(), "layout fixture is empty");
    xml.insert(xml.find("</Style>"), "<noteBarDistance>9</noteBarDistance>");
    xml.insert(xml.find("<Chord>"),
               "<Capo><active>1</active><fretPosition>3</fretPosition><transposeMode>0</transposeMode></Capo>");
    muse::ByteArray zipData;
    muse::io::Buffer output(&zipData);
    output.open(muse::io::IODevice::WriteOnly);
    {
        muse::ZipWriter writer(&output);
        writer.addFile("score.mscx", muse::ByteArray(xml.c_str(), xml.size()));
        writer.close();
        require(!writer.hasError(), "layout fixture ZIP failed");
    }
    output.close();

    // Use the actual desktop engine setup as the reference, independently of Session.
    auto project = EngravingProject::create(muse::modularity::globalCtx());
    project->setFileInfoProvider(mscz::makeStreamFileInfoProvider({"score.mscz"}));
    muse::io::Buffer device(&zipData);
    MscReader::Params params; params.device = &device; params.mode = MscIoMode::Zip;
    MscReader reader(params);
    require(bool(reader.open()) && bool(project->loadMscz(reader, nullptr, false)), "reference could not load");
    auto score = project->masterScore();
    const double expectedPadding = score->style().styleMM(Sid::noteBarDistance);
    require(std::abs(score->paddingTable().at(ElementType::NOTE).at(ElementType::BAR_LINE) - expectedPadding) > 1,
            "fixture no longer exposes stale spacing before desktop setup");
    require(!score->staff(0)->capo(Fraction(0, 1)).active, "fixture already has capo state before setup");
    score->lockUpdates(true);
    require(bool(project->setupMasterScore(true)), "reference setup failed");
    require(std::abs(score->paddingTable().at(ElementType::NOTE).at(ElementType::BAR_LINE) - expectedPadding) < 1e-6,
            "desktop setup did not apply saved spacing");
    compat::EngravingCompat::doPreLayoutCompatIfNeeded(score);
    score->updateCapo(true);
    require(score->staff(0)->capo(Fraction(0, 1)).active
            && score->staff(0)->capo(Fraction(0, 1)).fretPosition == 3, "saved capo was not restored");
    score->lockUpdates(false); score->setLayoutAll(); score->update();
    compat::EngravingCompat::doPostLayoutCompatIfNeeded(score);
    std::vector<std::string> expected;
    auto rounded = [score](double v) { return std::to_string(std::llround(v / (score->style().spatium() * 0.1))); };
    int pageNumber = 0;
    for (const auto page : score->pages()) {
        for (const auto item : page->elements()) {
            if (!item || !item->visible() || item->isPage() || item->isTimeTickAnchor()) continue;
            auto rect = item->pageBoundingRect();
            expected.push_back(std::to_string(pageNumber) + " " + item->typeName() + " "
                               + rounded(rect.left()) + " " + rounded(rect.top()) + " " + rounded(rect.width()));
        }
        ++pageNumber;
    }
    std::sort(expected.begin(), expected.end());
    std::string error;
    auto session = mscz::Session::open(std::make_unique<MemoryScore>(zipData), {"score.mscz"}, error);
    require(bool(session), error.c_str());
    std::vector<std::string> actual;
    require(session->layoutFingerprint(actual, error), error.c_str());
    require(actual == expected, "Session layout differs from desktop setup with saved spacing and capo");
}
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
        auto resources = mscz::makeCachingProvider(
            mscz::makeDirectoryProvider(MSCZ_RESOURCE_ROOT, MSCZ_RESOURCE_MANIFEST));
        for (const char* version : {"v1", "v2", "v3", "v302", "v450"}) {
            require(resources->exists(std::string(":/engraving/styles/legacy-style-defaults-") + version + ".mss"),
                    "a legacy style is not registered as an engine resource");
        }
        mscz::Engine::setResourceProvider(resources);
        mscz::Engine::initializeIfNeeded();
        require(DefaultStyle::resolveStyleDefaults(450).styleD(Sid::spacingDensity) == 1.30,
                "4.50 style defaults were not loaded from the resource");
        checkDesktopLoadParity();
        for (const char* version : {"4.50", "4.60", "4.70"}) {
            const std::string xml = std::string("<museScore version=\"") + version
                + "\"><Style><rehearsalMarkAlign>right,baseline</rehearsalMarkAlign>"
                  "<rehearsalMarkPosition>center</rehearsalMarkPosition></Style></museScore>";
            muse::ByteArray bytes(xml.c_str(), xml.size());
            muse::io::Buffer buffer(&bytes); buffer.open(muse::io::IODevice::ReadOnly);
            MStyle style;
            require(style.read(&buffer), "rehearsal style could not load");
            const auto expected = std::string(version) == "4.70" ? AlignH::HCENTER : AlignH::RIGHT;
            require(style.styleV(Sid::rehearsalMarkPosition).value<AlignH>() == expected,
                    "legacy rehearsal alignment or independent modern position changed");
        }
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
        for (int arg = 1; arg < argc; ++arg) {
            std::string error;
            auto score = mscz::Session::openPath(argv[arg], error);
            require(bool(score), error.c_str());
            require(score->pageCount() == 1, "reference score must fit on one page");
        }
        std::cout << "PASS desktop loading parity, saved spacing/capo, standard compatibility and pagination\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
