// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
namespace mu::engraving { class MasterScore; }
namespace mscz {
bool playbackTimeline(mu::engraving::MasterScore*, std::string&, std::string&);
}
