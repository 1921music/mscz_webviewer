/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "mscz_engine.h"
#include "displaytranspose.h"
#ifdef MSCZ_ENABLE_PLAYBACK
#include "playbacktimeline.h"
#endif
#include "textfontbackend.h"
#include "musicxmlimport.h"
#include "importexport/musicxml/internal/musicxmlconfiguration.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include "global/serialization/json.h"
#include "global/io/buffer.h"
#include "engraving/infrastructure/mscwriter.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/system.h"
#include "engraving/dom/image.h"
#include "engraving/dom/imageStore.h"
#include "engraving/dom/staff.h"

#ifndef NO_QT_SUPPORT
#include <QBuffer>
#include <QByteArray>
#include <QGuiApplication>
#include <QImage>
#endif

#include "global/globalmodule.h"
#include "global/modularity/ioc.h"

#include "draw/drawmodule.h"
#include "draw/painter.h"

#include "engraving/engravingmodule.h"
#include "engraving/engravingproject.h"
#include "engraving/compat/engravingcompat.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/dom/engravingitem.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/page.h"
#include "engraving/editing/transpose.h"
#include "engraving/rendering/iscorerenderer.h"

#include "mscz_app_stub.h"
#include "svgpaintprovider.h"
#include "draw/ifontprovider.h"

#ifdef NO_QT_SUPPORT
#include "compatfontprovider.h"
#include "draw/ifontprovider.h"
#endif

namespace {

constexpr muse::IApplication::RunMode kRunMode = muse::IApplication::RunMode::ConsoleApp;

std::shared_ptr<mscz::IResourceProvider> g_resources;

// Readers use absolute offsets, so independent devices can replay the same
// read-only source without copying the container or sharing a seek cursor.
class SharedReadStream : public mscz::IReadStream
{
public:
    explicit SharedReadStream(std::shared_ptr<mscz::IReadStream> source) : m_source(std::move(source)) {}
    uint64_t size() const override { return m_source->size(); }
    size_t read(uint8_t* dest, uint64_t pos, size_t len) override { return m_source->read(dest, pos, len); }
private:
    std::shared_ptr<mscz::IReadStream> m_source;
};

//! Brings up the muse framework the way aria's RenderRuntime does: global,
//! draw and engraving, in that order, through the full module lifecycle.
//! Qt is still in the picture in Phase 1, hence the QGuiApplication.
class Runtime
{
public:
    static Runtime& instance()
    {
        static Runtime runtime;
        return runtime;
    }

private:
    Runtime()
    {
#ifndef NO_QT_SUPPORT
        // Only the Qt-backed build needs an application object: the font
        // provider and the raster painter are Qt's there. The Qt-free build
        // uses the FreeType provider and paints only to SVG.
        if (!QGuiApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "mscz_test";
            static char* argv[] = { arg0, nullptr };
            m_qapp = std::make_unique<QGuiApplication>(argc, argv);
        }
#endif

        m_modules.emplace_back(std::make_unique<muse::draw::DrawModule>());
        m_modules.emplace_back(std::make_unique<mu::engraving::EngravingModule>());

        // Let GlobalModule register the actual application once, rather than
        // pre-registering it and colliding with the module's own stub.
        auto application = std::make_shared<mscz::AppStub>();
        m_globalModule.setApplication(application);
        for (const auto& module : m_modules) {
            module->setApplication(application);
        }

        IF_ASSERT_FAILED(g_resources) {
            LOGE() << "no resource provider set; call Engine::setResourceProvider first";
        }

        m_globalModule.registerResources();
        m_globalModule.registerExports();
        for (const auto& module : m_modules) {
            module->registerResources();
            module->registerExports();
        }

        // Replace module defaults before any imports are resolved or modules
        // initialized. unregister is also valid when the Qt-free global
        // module provides no filesystem. Do not rely on rejected duplicate
        // registrations to choose our implementations.
        auto ioc = muse::modularity::globalIoc();
        auto musicXmlConfig = std::make_shared<mu::iex::musicxml::MusicXmlConfiguration>();
        ioc->registerExport<mu::iex::musicxml::IMusicXmlConfiguration>("mscz_test", musicXmlConfig);
        ioc->unregister<muse::io::IFileSystem>("mscz_test");
        ioc->registerExport<muse::io::IFileSystem>("mscz_test", new mscz::HostFileSystem(g_resources));
#ifdef NO_QT_SUPPORT
        std::shared_ptr<muse::draw::IFontProvider> fontProvider = std::make_shared<mscz::CompatFontProvider>();
#else
        auto fontProvider = ioc->resolve<muse::draw::IFontProvider>("mscz_test");
#endif
        ioc->unregister<muse::draw::IFontProvider>("mscz_test");
        ioc->registerExport<muse::draw::IFontProvider>("mscz_test", mscz::routeTextFonts(std::move(fontProvider)));

        m_globalModule.resolveImports();
        for (const auto& module : m_modules) {
            module->resolveImports();
        }

        m_globalModule.onPreInit(kRunMode);
        for (const auto& module : m_modules) {
            module->onPreInit(kRunMode);
        }

        m_globalModule.onInit(kRunMode);
        musicXmlConfig->init();
        for (const auto& module : m_modules) {
            module->onInit(kRunMode);
        }

        // Instrument names and ordering come from these; without them staves
        // render with no instrument label.
        const bool instruments = mu::engraving::loadInstrumentTemplates(":/engraving/instruments/instruments.xml");
        const bool orders = mu::engraving::loadInstrumentTemplates(":/engraving/instruments/orders.xml");
        if (!instruments || !orders) {
            std::cerr << "mscz_test: instrument templates failed to load"
                      << " (instruments=" << instruments << " orders=" << orders << ")" << std::endl;
        }

        m_globalModule.onAllInited(kRunMode);
        for (const auto& module : m_modules) {
            module->onAllInited(kRunMode);
        }

        m_globalModule.onStartApp();
        for (const auto& module : m_modules) {
            module->onStartApp();
        }
    }

    ~Runtime()
    {
        for (const auto& module : m_modules) {
            module->onDeinit();
        }
        m_globalModule.onDeinit();

        for (const auto& module : m_modules) {
            module->onDestroy();
        }
        m_globalModule.onDestroy();
    }

#ifndef NO_QT_SUPPORT
    std::unique_ptr<QGuiApplication> m_qapp;
#endif
    muse::GlobalModule m_globalModule;
    std::vector<std::unique_ptr<muse::modularity::IModuleSetup> > m_modules;
};

} // namespace

namespace mscz {

void Engine::setResourceProvider(std::shared_ptr<IResourceProvider> resources)
{
    g_resources = std::move(resources);
}

void Engine::initializeIfNeeded()
{
    (void)Runtime::instance();
}

class Session::Impl
{
public:
    std::shared_ptr<IReadStream> source;
    ScoreInfo info;
    //! Outlives the score: the ZIP layer reads through it on demand.
    std::unique_ptr<StreamDevice> device;
    mu::engraving::EngravingProjectPtr project;
    mu::engraving::MasterScore* masterScore() const { return project->masterScore(); }
    int pageCount = 0;
    int transposeSemitones = 0;
    TransposeSpelling transposeSpelling = TransposeSpelling::Readable;

    mu::engraving::rendering::IScoreRenderer* renderer() const
    {
        return m_renderer().get();
    }

private:
    muse::ContextInject<mu::engraving::rendering::IScoreRenderer> m_renderer { muse::modularity::globalCtx() };
};

Session::Session(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Session::~Session() = default;

#ifdef MSCZ_ENABLE_PLAYBACK
bool Session::playbackTimeline(std::string& out, std::string& error)
{
    return mscz::playbackTimeline(m_impl->masterScore(), out, error);
}
#endif

std::unique_ptr<Session> Session::open(std::unique_ptr<IReadStream> score, const ScoreInfo& info,
                                      std::string& error)
{
    return openShared(std::move(score), info, error);
}

std::unique_ptr<Session> Session::openShared(std::shared_ptr<IReadStream> score, const ScoreInfo& info,
                                            std::string& error)
{
    if (!score) {
        error = "No score to read.";
        return nullptr;
    }

    Engine::initializeIfNeeded();

    auto impl = std::make_unique<Impl>();
    impl->source = std::move(score);
    impl->info = info;
    impl->project = mu::engraving::EngravingProject::create(muse::modularity::globalCtx());
    if (!impl->masterScore()) {
        error = "Could not create a score.";
        return nullptr;
    }

    // Not compat::loadMsczOrMscx: that takes a path and opens an io::File,
    // which reads the whole thing. Going through MscReader with a device lets
    // the ZIP layer seek and inflate only the entries it reads.
    impl->device = std::make_unique<StreamDevice>(std::make_unique<SharedReadStream>(impl->source));

    // Imported scores share the desktop's final setup/layout below, but do
    // not go through the native MSC container reader or its legacy rewrites.
    impl->project->setFileInfoProvider(makeStreamFileInfoProvider(info));
    muse::Ret ret;
    const bool musicXml = isMusicXml(info);
    if (musicXml) {
        impl->masterScore()->checkChordList();
        ret = importMusicXml(impl->masterScore(), *impl->device, info);
    } else {
        mu::engraving::MscReader::Params params;
        params.device = impl->device.get();
        params.mode = mu::engraving::MscIoMode::Zip;

        mu::engraving::MscReader reader(params);
        ret = reader.open();
        if (!ret) {
            error = "Could not read the score container: " + ret.text();
            return nullptr;
        }

        ret = impl->project->loadMscz(reader, nullptr, false);
        if (!ret) {
            // The second chance desktop MuseScore's "open anyway" gives a
            // file written by a newer version.
            ret = impl->project->loadMscz(reader, nullptr, true);
        }
    }
    if (!ret) {
        error = "Could not read the score: " + ret.text();
        return nullptr;
    }

    // Follow NotationProject::doLoad, including EngravingProject's final
    // padding-table rebuild and tie cleanup after the styles and parts load.
    // Lock updates as the desktop does, so setup does not lay out before compat.
    auto masterScore = impl->masterScore();
    masterScore->lockUpdates(true);
    // Keep the viewer's existing ability to display recoverable scores.
    ret = impl->project->setupMasterScore(/*forceMode=*/true);
    if (!ret) {
        error = "Could not prepare the score: " + ret.text();
        return nullptr;
    }
    if (!musicXml) { mu::engraving::compat::EngravingCompat::doPreLayoutCompatIfNeeded(masterScore); }
    masterScore->updateCapo(/*ignoreNotationUpdate=*/true);
    masterScore->lockUpdates(false);
    masterScore->setLayoutAll();
    masterScore->update();
    if (!musicXml) { mu::engraving::compat::EngravingCompat::doPostLayoutCompatIfNeeded(masterScore); }
    impl->pageCount = static_cast<int>(masterScore->pages().size());

    return std::unique_ptr<Session>(new Session(std::move(impl)));
}

std::unique_ptr<Session> Session::openPath(const std::string& scorePath, std::string& error)
{
    Engine::initializeIfNeeded();

    if (!g_resources) {
        error = "No resource provider set.";
        return nullptr;
    }

    std::unique_ptr<IReadStream> stream = g_resources->open(scorePath);
    if (!stream) {
        error = "Could not open " + scorePath;
        return nullptr;
    }

    ScoreInfo info;
    info.name = muse::io::filename(muse::io::path_t(scorePath)).toStdString();
    return open(std::move(stream), info, error);
}

int Session::pageCount() const
{
    return m_impl->pageCount;
}

int Session::viewTransposeSemitones() const
{
    return m_impl->transposeSemitones;
}

bool Session::setViewTransposeSemitones(int semitones, std::string& error)
{
    return setViewTranspose(semitones, m_impl->transposeSpelling, error);
}

Session::TransposeSpelling Session::viewTransposeSpelling() const
{
    return m_impl->transposeSpelling;
}

bool Session::setViewTranspose(int semitones, TransposeSpelling spelling, std::string& error)
{
    error.clear();
    if (spelling != TransposeSpelling::Preserve && spelling != TransposeSpelling::Readable) {
        error = "Unknown transposition spelling mode.";
        return false;
    }
    if (semitones < -12 || semitones > 12) {
        error = "Display transposition must be between -12 and +12 semitones.";
        return false;
    }
    if (semitones == m_impl->transposeSemitones && spelling == m_impl->transposeSpelling) {
        return true;
    }

    // Always start from the original source. Repeated half-step operations
    // otherwise accumulate enharmonic spelling and layout changes. Build a
    // candidate first so failed transpositions cannot damage the visible score.
    auto candidate = openShared(m_impl->source, m_impl->info, error);
    if (!candidate) {
        return false;
    }
    if (semitones != 0) {
        using namespace mu::engraving;
        auto score = candidate->m_impl->masterScore();
        if (score->nstaves() == 0) {
            error = "The score has no staves to transpose.";
            return false;
        }
        static constexpr int intervals[] = { 0, 3, 4, 7, 8, 11, 12, 14, 17, 18, 21, 22, 25 };
        auto interval = Interval::allIntervals[intervals[std::abs(semitones)]];
        if (semitones < 0) { interval.flip(); }
        spelling::TranspositionSpelling policy(score, interval.chromatic * 7 - interval.diatonic * 12);
        score->startCmd(muse::TranslatableString("mscz_test", "Transpose display"));
        score->cmdSelectAll();
        const bool ok = Transpose::transpose(score, TransposeMode::BY_INTERVAL,
                                             semitones > 0 ? TransposeDirection::UP : TransposeDirection::DOWN,
                                             Key::C, intervals[std::abs(semitones)], true, true, false);
        if (ok) { policy.apply(spelling == TransposeSpelling::Readable); }
        const bool engineError = MScore::_error != MsError::MS_NO_ERROR && !MScore::_errorIsWarning;
        score->endCmd(!ok || engineError);
        if (!ok || engineError) {
            error = "The score cannot be transposed by that interval (a note may be out of range).";
            return false;
        }
        score->deselectAll();
        score->setLayoutAll();
        score->doLayout();
        candidate->m_impl->pageCount = static_cast<int>(score->pages().size());
    }
    candidate->m_impl->transposeSemitones = semitones;
    candidate->m_impl->transposeSpelling = spelling;
    m_impl.swap(candidate->m_impl);
    return true;
}

bool Session::layoutFingerprint(std::vector<std::string>& out, std::string& error) const
{
    out.clear();

    const mu::engraving::Score* score = m_impl->masterScore();
    if (!score) {
        error = "The score is closed.";
        return false;
    }

    const double unit = score->style().spatium() * 0.1;
    if (unit <= 0.0) {
        error = "The score has no usable spatium.";
        return false;
    }

    auto rounded = [unit](const double value) {
        return static_cast<long long>(std::llround(value / unit));
    };

    int pageNumber = 0;
    for (const mu::engraving::Page* page : score->pages()) {
        for (const mu::engraving::EngravingItem* item : page->elements()) {
            // Time-tick anchors are drag guides left in the layout; nothing draws them.
            if (!item || !item->visible() || item->isPage() || item->isTimeTickAnchor()) {
                continue;
            }

            const mu::engraving::RectF rect = item->pageBoundingRect();
            out.push_back(std::to_string(pageNumber) + " " + item->typeName()
                          + " " + std::to_string(rounded(rect.left()))
                          + " " + std::to_string(rounded(rect.top()))
                          + " " + std::to_string(rounded(rect.width())));
        }
        ++pageNumber;
    }

    std::sort(out.begin(), out.end());
    return true;
}

bool Session::renderPageSvg(int pageIndex, int dpi, SvgSymbols symbols, SvgFonts fonts,
                            std::string& out, std::string& error) const
{
    out.clear();

    if (dpi <= 0) {
        error = "The DPI must be positive.";
        return false;
    }
    if (pageIndex < 0 || pageIndex >= m_impl->pageCount) {
        error = "The requested page is outside the score.";
        return false;
    }

    mu::engraving::rendering::IScoreRenderer* renderer = m_impl->renderer();
    if (!renderer) {
        error = "The score renderer is unavailable.";
        return false;
    }

    mu::engraving::Score* score = m_impl->masterScore();

    mu::engraving::rendering::IScoreRenderer::ScorePaintOptions options;
    options.fromPage = pageIndex;
    options.toPage = pageIndex;
    options.deviceDpi = dpi;
    options.printPageBackground = false;
    options.isSetViewport = true;
    options.isMultiPage = false;
    options.isPrinting = true;

    auto provider = std::make_shared<SvgPaintProvider>(
        symbols == SvgSymbols::Path ? SvgPaintProvider::SymbolMode::Path
        : SvgPaintProvider::SymbolMode::Font,
        fonts == SvgFonts::EmbedSubset ? SvgPaintProvider::FontMode::EmbedSubset
        : SvgPaintProvider::FontMode::Reference);
    {
        muse::draw::Painter painter(provider, "mscz_test_svg");
        renderer->paintScore(&painter, score, options);
        painter.endDraw();
    }

    out = provider->svg();
    if (out.empty()) {
        error = "The SVG provider produced nothing.";
        return false;
    }
    return true;
}

#ifndef NO_QT_SUPPORT
bool Session::renderPage(int pageIndex, int dpi, PageImage& out, std::string& error) const
{
    if (dpi <= 0) {
        error = "The DPI must be positive.";
        return false;
    }
    if (pageIndex < 0 || pageIndex >= m_impl->pageCount) {
        error = "The requested page is outside the score.";
        return false;
    }

    mu::engraving::rendering::IScoreRenderer* renderer = m_impl->renderer();
    if (!renderer) {
        error = "The score renderer is unavailable.";
        return false;
    }

    mu::engraving::Score* score = m_impl->masterScore();

    mu::engraving::rendering::IScoreRenderer::ScorePaintOptions options;
    options.fromPage = pageIndex;
    options.toPage = pageIndex;
    options.deviceDpi = dpi;
    options.printPageBackground = false;
    options.isSetViewport = true;
    options.isMultiPage = false;
    options.isPrinting = true;

    const muse::SizeF pageSizeInch = renderer->pageSizeInch(score, options);
    const int width = std::lrint(pageSizeInch.width() * dpi);
    const int height = std::lrint(pageSizeInch.height() * dpi);
    if (width <= 0 || height <= 0) {
        error = "The engine produced an invalid page size.";
        return false;
    }

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.setDotsPerMeterX(std::lrint((dpi * 1000.0) / mu::engraving::INCH));
    image.setDotsPerMeterY(std::lrint((dpi * 1000.0) / mu::engraving::INCH));
    image.fill(Qt::white);

    {
        muse::draw::Painter painter(&image, "mscz_test");
        renderer->paintScore(&painter, score, options);
    }

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "png")) {
        error = "Could not encode the page as PNG.";
        return false;
    }

    out.pageIndex = pageIndex;
    out.pixelWidth = width;
    out.pixelHeight = height;
    out.data.assign(png.begin(), png.end());
    return true;
}
#else
bool Session::renderPage(int, int, PageImage&, std::string& error) const
{
    // PNG output goes through QImage. The Qt-free build renders to SVG, which
    // is what the browser wants anyway; this exists so the API shape does not
    // change between builds.
    error = "PNG rendering needs the Qt-backed build; use renderPageSvg.";
    return false;
}
#endif

// Coordinates use the same 72-DPI viewport as the Rostrum page background.
// Segment keys are musical time (exact rational string), not native pointers.
bool Session::rostrumInfo(std::string& out, std::string& error, bool hideAnnotations)
{
    using namespace mu::engraving;
    using namespace muse;
    auto score = m_impl->masterScore();
    JsonObject root;
    JsonArray pages, anchors, images;
    auto pageIndex = [score](const Page* page) {
        auto it = std::find(score->pages().begin(), score->pages().end(), page);
        return int(it - score->pages().begin());
    };
    const auto inches = m_impl->renderer()->pageSizeInch(score);
    const double width = std::lrint(inches.width() * 72.0);
    const double height = std::lrint(inches.height() * 72.0);
    const double sx = width / std::lrint(inches.width() * DPI);
    const double sy = height / std::lrint(inches.height() * DPI);
    for (const auto* page : score->pages()) {
        (void)page;
        JsonObject p; p["width"] = width; p["height"] = height;
        pages.append(p);
    }
    for (auto* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        auto* system = s->measure()->system();
        if (!system || !system->page()) continue;
        for (size_t staff = 0; staff < score->nstaves(); ++staff) {
            if (!system->staff(staff)->show()) continue;
            JsonObject a;
            a["key"] = s->tick().toString().toStdString();
            a["staff"] = int(staff); a["page"] = pageIndex(system->page());
            a["system"] = system->pagePos().y() * sy;
            a["x"] = s->pageX() * sx;
            a["y"] = system->staffYpage(staff) * sy;
            a["sp"] = score->staff(staff)->spatium(s->tick()) * sx;
            a["spY"] = score->staff(staff)->spatium(s->tick()) * sy;
            anchors.append(a);
        }
    }
    score->scanElements([&](EngravingItem* item) {
        if (!item->isImage()) return;
        auto* image = toImage(item);
        if (!image->storeItem() || image->imageType() != ImageType::SVG) return;
        const auto& bytes = image->storeItem()->buffer();
        std::string svg(reinterpret_cast<const char*>(bytes.constData()), bytes.size());
        if (svg.find("https://1921music.github.io/rostrum/ns") == std::string::npos) return;
        JsonObject i;
        auto* anchorParent = image->parentItem();
        while (anchorParent && !anchorParent->isSegment()) anchorParent = anchorParent->parentItem();
        if (anchorParent) i["key"] = toSegment(anchorParent)->tick().toString();
        i["staff"] = int(image->staffIdx());
        auto* parent = image->parentItem();
        while (parent && !parent->isPage()) parent = parent->parentItem();
        if (!parent) return;
        i["svg"] = svg; i["page"] = pageIndex(toPage(parent));
        i["x"] = image->pagePos().x() * sx; i["y"] = image->pagePos().y() * sy;
        const double unit = image->sizeIsSpatium() ? image->spatium() : DPMM;
        i["width"] = image->size().width() * unit * sx;
        i["height"] = image->size().height() * unit * sy;
        images.append(i);
        if (hideAnnotations) image->setVisible(false);
    });
    root["pages"] = pages; root["anchors"] = anchors; root["images"] = images;
    root["settings"] = score->metaTag(u"rostrum:settings");
    const auto bytes = JsonDocument(root).toJson(JsonDocument::Format::Compact);
    out.assign(reinterpret_cast<const char*>(bytes.constData()), bytes.size());
    return true;
}

bool Session::saveRostrum(const std::string& data, std::string& out, std::string& error) const
{
    using namespace mu::engraving;
    using namespace muse;
    const auto doc = JsonDocument::fromJson(ByteArray(data.data(), data.size()), &error);
    if (!error.empty() || !doc.isObject()) return false;
    auto original = openShared(m_impl->source, m_impl->info, error);
    if (!original) return false;
    auto* score = original->m_impl->masterScore();
    std::vector<EngravingItem*> oldImages;
    score->scanElements([&](EngravingItem* item) {
        if (!item->isImage()) return;
        auto* image = toImage(item);
        if (!image->storeItem()) return;
        const auto& b = image->storeItem()->buffer();
        std::string svg(reinterpret_cast<const char*>(b.constData()), b.size());
        if (svg.find("https://1921music.github.io/rostrum/ns") != std::string::npos) oldImages.push_back(item);
    });
    for (auto* image : oldImages) { image->parentItem()->remove(image); delete image; }
    const auto root = doc.rootObject();
    score->setMetaTag(u"rostrum:settings", root.value("settings").toString());
    const auto annotations = root.value("annotations").toArray();
    for (size_t n = 0; n < annotations.size(); ++n) {
        const auto a = annotations.at(n).toObject();
        const int staff = a.value("staff").toInt();
        Segment* target = nullptr;
        for (auto* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            if (s->tick().toString() == a.value("key").toString()) { target = s; break; }
        }
        if (!target || staff < 0 || staff >= int(score->nstaves())) {
            error = "An annotation anchor no longer exists; the score was not saved."; return false;
        }
        const auto svg = a.value("svg").toStdString();
        auto* image = new Image(target);
        image->setTrack(staff * VOICES);
        image->loadFromData("rostrum.svg", ByteArray(svg.data(), svg.size()));
        image->setSizeIsSpatium(true); image->setAutoScale(false);
        image->setSize(SizeF(a.value("width").toDouble(), a.value("height").toDouble()));
        image->setPropertyFlags(Pid::OFFSET, PropertyFlags::UNSTYLED);
        image->setOffset(PointF(a.value("dx").toDouble(), a.value("dy").toDouble()) * image->spatium());
        image->setAutoplace(false);
        target->add(image);
    }
    // No thumbnail rendering: the Qt-free SVG renderer need not interpret the
    // annotation images, and the source bytes must remain unchanged.
    ByteArray bytes;
    io::Buffer buffer(&bytes); buffer.open(io::IODevice::ReadWrite);
    MscWriter::Params params; params.device = &buffer; params.mainFileName = u"score.mscx";
    MscWriter writer(params);
    if (!writer.open() || !original->m_impl->project->writeMscz(writer, false)) {
        error = "Could not write MSCZ."; return false;
    }
    writer.close();
    if (writer.hasError()) { error = "Could not finish MSCZ."; return false; }
    out.assign(reinterpret_cast<const char*>(bytes.constData()), bytes.size());
    return true;
}

} // namespace mscz
