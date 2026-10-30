/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * Added for mscz_test. The stock TickerProvider is built on QTimer.
 */
#ifndef MUSE_GLOBAL_TICKERPROVIDERSTUB_H
#define MUSE_GLOBAL_TICKERPROVIDERSTUB_H

#include "itickerprovider.h"

namespace muse {
//! A Qt-free ITickerProvider that does nothing.
//!
//! Tickers drive periodic work in a running application. This build opens a
//! score, lays it out and paints it, all in one call, so no task would ever
//! come due. Tasks are accepted and dropped rather than refused, so a caller
//! that registers one is not broken by it.
class TickerProviderStub : public ITickerProvider
{
public:
    void start() override {}
    void stop() override {}
    void forceSchedule() override {}

    uint32_t addTask(const Task&) override { return ++m_nextId; }
    void removeTask(const uint32_t&) override {}

private:
    uint32_t m_nextId = 0;
};
}

#endif // MUSE_GLOBAL_TICKERPROVIDERSTUB_H
