// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "hostio.h"
#include "types/ret.h"

namespace mu::engraving { class MasterScore; }
namespace mscz {
// The format comes from the supplied file name; all bytes still come from
// the host's stream. Reopening for absolute transposition uses the same path.
bool isMusicXml(const ScoreInfo& info);
muse::Ret importMusicXml(mu::engraving::MasterScore* score, StreamDevice& device, const ScoreInfo& info);
}
