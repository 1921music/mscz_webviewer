/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * Added for mscz_test. The stock GlobalConfiguration answers with
 * QCoreApplication and QStandardPaths.
 */
#ifndef MUSE_GLOBAL_GLOBALCONFIGURATIONSTUB_H
#define MUSE_GLOBAL_GLOBALCONFIGURATIONSTUB_H

#include "iglobalconfiguration.h"

namespace muse {
//! A Qt-free IGlobalConfiguration that answers with nothing.
//!
//! Every path it offers is an installed application's: where the binary sits,
//! the user's config folder, their home and downloads directories. None of
//! that exists for a renderer embedded in something else, and in a browser
//! none of it exists at all. Resources come from the host's provider by name,
//! not by assembling paths, so these go unused -- the one caller in this build
//! is EngravingConfiguration::appDataPath(), whose result only feeds further
//! path building.
//!
//! Returning empty rather than guessing keeps a wrong path from being quietly
//! used, since an empty path fails loudly at the point of use.
class GlobalConfigurationStub : public IGlobalConfiguration
{
public:
    io::path_t appBinPath() const override { return io::path_t(); }
    io::path_t appBinDirPath() const override { return io::path_t(); }
    io::path_t appDataPath() const override { return io::path_t(); }
    io::path_t appConfigPath() const override { return io::path_t(); }

    io::path_t userAppDataPath() const override { return io::path_t(); }
    io::path_t userBackupPath() const override { return io::path_t(); }
    io::path_t userDataPath() const override { return io::path_t(); }

    io::path_t homePath() const override { return io::path_t(); }
    io::path_t downloadsPath() const override { return io::path_t(); }

    bool useFactorySettings() const override { return true; }
    bool enableExperimental() const override { return false; }
    io::path_t genericDataPath() const override { return io::path_t(); }

    bool devModeEnabled() const override { return false; }
    void setDevModeEnabled(bool) override {}

    bool metricUnit() const override { return true; }
    void setMetricUnit(bool) override {}

    std::string museScoreUrl() const override { return std::string(); }
    std::string museHubWebUrl() const override { return std::string(); }

    bool highResolutionTimers() const override { return false; }
};
}

#endif // MUSE_GLOBAL_GLOBALCONFIGURATIONSTUB_H
