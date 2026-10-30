/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "drawmodule.h"

#include "global/modularity/ioc.h"

#ifndef DRAW_NO_INTERNAL
#ifndef NO_QT_SUPPORT
#include "internal/qfontprovider.h"
#include "internal/qimageprovider.h"
#endif
#include "internal/fontsdatabase.h"
#ifndef DRAW_QT_FONT_PROVIDER_ONLY
#include "internal/fontprovider.h"
#include "internal/fontsengine.h"
#endif
#endif

#include "muse_framework_config.h"

using namespace muse::draw;
using namespace muse::modularity;

std::string DrawModule::moduleName() const
{
    return "draw";
}

void DrawModule::registerExports()
{
#ifndef DRAW_NO_INTERNAL

    ioc()->registerExport<draw::IFontsDatabase>(moduleName(), new FontsDatabase());

#ifndef DRAW_QT_FONT_PROVIDER_ONLY
    m_fontsEngine = std::make_shared<FontsEngine>(iocContext());
    ioc()->registerExport<draw::IFontsEngine>(moduleName(), m_fontsEngine);
#endif

#ifndef NO_QT_SUPPORT
    ioc()->registerExport<draw::IImageProvider>(moduleName(), new QImageProvider());
    ioc()->registerExport<draw::IFontProvider>(moduleName(), std::make_shared<QFontProvider>());
#else
    //! NOTE mscz_test: this is the Qt removal. Upstream always registers
    //! QFontProvider as IFontProvider -- FontProviderDispatcher, which looks
    //! like it splits the metrics between Qt and FreeType, is never
    //! instantiated -- so every font metric the layout is built on comes from
    //! Qt. Here it comes from FontProvider, which answers out of FontsEngine
    //! and so out of FreeType and HarfBuzz.
    //!
    //! EngravingFont::computeMetrics takes every symbol's bounding box and
    //! advance from this, so the layout can move. scripts/fingerprint.sh is
    //! the check for that.
    //!
    //! No IImageProvider: it wraps QImage, and nothing we build resolves it.
    ioc()->registerExport<draw::IFontProvider>(moduleName(), std::make_shared<FontProvider>(iocContext()));
#endif

#endif // DRAW_NO_INTERNAL
}

void DrawModule::onInit(const IApplication::RunMode&)
{
#ifndef DRAW_NO_INTERNAL
#ifndef DRAW_QT_FONT_PROVIDER_ONLY
    m_fontsEngine->init();
#endif
#endif // DRAW_NO_INTERNAL
}
