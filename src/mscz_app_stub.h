/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#pragma once

#include "global/iapplication.h"

namespace mscz {
//! There is no MuseScore application here, but the engine still asks
//! IApplication for the version it stamps into saved files
//! (<programVersion>, <programRevision>). Report the MuseScore release the
//! vendored engine derives from, so anything we write carries the header that
//! release writes. Ported from aria's score_render_application_stub.h.
class AppStub : public muse::IApplication
{
public:
    muse::String name() const override { return u"MuseScore Studio"; }
    muse::String title() const override { return u"MuseScore Studio"; }
    bool unstable() const override { return false; }
    muse::Version version() const override { return muse::Version(4, 7, 5); }
    muse::Version fullVersion() const override { return version(); }

    muse::String build() const override { return u""; }
    muse::String revision() const override { return u"3654226"; }

    RunMode runMode() const override { return RunMode::ConsoleApp; }
    bool noGui() const override { return true; }

    void setup() override {}
    void finish() override {}
    void restart() override {}

    muse::modularity::ContextPtr setupNewContext() override { return nullptr; }
    int contextCount() const override { return 0; }
    std::vector<muse::modularity::ContextPtr> contexts() const override { return {}; }

    const muse::modularity::ContextPtr iocContext() const override { return muse::modularity::globalCtx(); }
    muse::modularity::ModulesIoC* ioc() const override { return muse::modularity::globalIoc(); }

    void processEvents() override {}

#ifndef NO_QT_SUPPORT
    QWindow* focusWindow() const override { return nullptr; }
    bool notify(QObject*, QEvent*) override { return true; }

    Qt::KeyboardModifiers keyboardModifiers() const override { return Qt::KeyboardModifiers(); }
#endif
};
} // namespace mscz
