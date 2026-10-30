/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "mscz_engine.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <QBuffer>
#include <QByteArray>
#include <QGuiApplication>
#include <QImage>

#include "global/globalmodule.h"
#include "global/modularity/ioc.h"

#include "draw/drawmodule.h"
#include "draw/painter.h"

#include "engraving/engravingmodule.h"
#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/dom/engravingitem.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/page.h"
#include "engraving/rendering/iscorerenderer.h"

#include "mscz_app_stub.h"
#include "svgpaintprovider.h"

namespace {

constexpr muse::IApplication::RunMode kRunMode = muse::IApplication::RunMode::ConsoleApp;

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
        if (!QGuiApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "mscz_test";
            static char* argv[] = { arg0, nullptr };
            m_qapp = std::make_unique<QGuiApplication>(argc, argv);
        }

        m_modules.emplace_back(std::make_unique<muse::draw::DrawModule>());
        m_modules.emplace_back(std::make_unique<mu::engraving::EngravingModule>());

        muse::modularity::globalIoc()->registerExport<muse::IApplication>("mscz_test", new mscz::AppStub());

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

    std::unique_ptr<QGuiApplication> m_qapp;
    muse::GlobalModule m_globalModule;
    std::vector<std::unique_ptr<muse::modularity::IModuleSetup> > m_modules;
};

} // namespace

namespace mscz {

void Engine::initializeIfNeeded()
{
    (void)Runtime::instance();
}

class Session::Impl
{
public:
    std::unique_ptr<mu::engraving::MasterScore> masterScore;
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

std::unique_ptr<Session> Session::open(const std::string& scorePath, std::string& error)
{
    Engine::initializeIfNeeded();

    auto impl = std::make_unique<Impl>();
    impl->masterScore = std::unique_ptr<mu::engraving::MasterScore>(
        mu::engraving::compat::ScoreAccess::createMasterScoreWithDefaultStyle(muse::modularity::globalCtx()));
    if (!impl->masterScore) {
        error = "Could not create a score.";
        return nullptr;
    }

    const muse::io::path_t path(scorePath);
    muse::Ret ret = mu::engraving::compat::loadMsczOrMscx(impl->masterScore.get(), path, false);
    if (!ret) {
        // Same second chance desktop MuseScore's "open anyway" gives a file
        // written by a newer version.
        ret = mu::engraving::compat::loadMsczOrMscx(impl->masterScore.get(), path, true);
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

bool Session::renderPageSvg(int pageIndex, int dpi, SvgSymbols symbols, std::string& out,
                            std::string& error) const
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
        : SvgPaintProvider::SymbolMode::Font);
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

} // namespace mscz
