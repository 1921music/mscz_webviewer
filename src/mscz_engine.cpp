/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "mscz_engine.h"

#include <algorithm>
#include <cmath>
#include <iostream>

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
#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/rw/mscloader.h"
#include "engraving/dom/engravingitem.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/page.h"
#include "engraving/rendering/iscorerenderer.h"

#include "mscz_app_stub.h"
#include "svgpaintprovider.h"

#ifdef NO_QT_SUPPORT
#include "compatfontprovider.h"
#include "draw/ifontprovider.h"
#endif

namespace {

constexpr muse::IApplication::RunMode kRunMode = muse::IApplication::RunMode::ConsoleApp;

std::shared_ptr<mscz::IResourceProvider> g_resources;

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

        muse::modularity::globalIoc()->registerExport<muse::IApplication>("mscz_test", new mscz::AppStub());

        //! NOTE Registered before the modules, because the ioc keeps the first
        //! registration. With Qt off the engine offers none at all; with Qt on
        //! global would offer a QFile-backed one, and this takes precedence so
        //! both builds read through the same host-side route.
        IF_ASSERT_FAILED(g_resources) {
            LOGE() << "no resource provider set; call Engine::setResourceProvider first";
        }
        muse::modularity::globalIoc()->registerExport<muse::io::IFileSystem>(
            "mscz_test", new mscz::HostFileSystem(g_resources));

#ifdef NO_QT_SUPPORT
        //! NOTE Registered before the draw module, so this wins over the
        //! FreeType provider that module would otherwise install. That one
        //! converts point size with muse::draw::DPI (360) and has none of
        //! QFontProvider's conventions, which is what moved almost every
        //! element on the page. CompatFontProvider answers as QFontProvider
        //! did, so the layout the engine was written for is preserved.
        muse::modularity::globalIoc()->registerExport<muse::draw::IFontProvider>(
            "mscz_test", std::make_shared<mscz::CompatFontProvider>());
#endif

        m_globalModule.registerResources();
        m_globalModule.registerExports();
        for (const auto& module : m_modules) {
            module->registerResources();
            module->registerExports();
        }

        m_globalModule.resolveImports();
        for (const auto& module : m_modules) {
            module->resolveImports();
        }

        m_globalModule.onPreInit(kRunMode);
        for (const auto& module : m_modules) {
            module->onPreInit(kRunMode);
        }

        m_globalModule.onInit(kRunMode);
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
    std::unique_ptr<mu::engraving::MasterScore> masterScore;
    //! Outlives the score: the ZIP layer reads through it on demand.
    std::unique_ptr<StreamDevice> device;
    int pageCount = 0;

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

std::unique_ptr<Session> Session::open(std::unique_ptr<IReadStream> score, const ScoreInfo& info,
                                      std::string& error)
{
    if (!score) {
        error = "No score to read.";
        return nullptr;
    }

    Engine::initializeIfNeeded();

    auto impl = std::make_unique<Impl>();
    impl->masterScore = std::unique_ptr<mu::engraving::MasterScore>(
        mu::engraving::compat::ScoreAccess::createMasterScoreWithDefaultStyle(muse::modularity::globalCtx()));
    if (!impl->masterScore) {
        error = "Could not create a score.";
        return nullptr;
    }

    // Not compat::loadMsczOrMscx: that takes a path and opens an io::File,
    // which reads the whole thing. Going through MscReader with a device lets
    // the ZIP layer seek and inflate only the entries it reads.
    impl->device = std::make_unique<StreamDevice>(std::move(score));

    mu::engraving::MscReader::Params params;
    params.device = impl->device.get();
    params.mode = mu::engraving::MscIoMode::Zip;

    mu::engraving::MscReader reader(params);
    muse::Ret ret = reader.open();
    if (!ret) {
        error = "Could not read the score container: " + ret.text();
        return nullptr;
    }

    // Not optional: MscLoader reads fileInfo()->fileName() while loading, and
    // a score opened from a stream has no file to take it from.
    impl->masterScore->setFileInfoProvider(makeStreamFileInfoProvider(info));

    mu::engraving::MscLoader loader;
    ret = loader.loadMscz(impl->masterScore.get(), reader, nullptr, false);
    if (!ret) {
        // The second chance desktop MuseScore's "open anyway" gives a file
        // written by a newer version.
        ret = loader.loadMscz(impl->masterScore.get(), reader, nullptr, true);
    }
    if (!ret) {
        error = "Could not read the score: " + ret.text();
        return nullptr;
    }

    impl->masterScore->setLayoutAll();
    impl->masterScore->doLayout();
    impl->pageCount = static_cast<int>(impl->masterScore->pages().size());

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

bool Session::layoutFingerprint(std::vector<std::string>& out, std::string& error) const
{
    out.clear();

    const mu::engraving::Score* score = m_impl->masterScore.get();
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

    mu::engraving::Score* score = m_impl->masterScore.get();

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

    mu::engraving::Score* score = m_impl->masterScore.get();

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

} // namespace mscz
