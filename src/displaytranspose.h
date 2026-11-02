// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <memory>
#include <vector>
#include "engraving/dom/pitchspelling.h"

namespace mu::engraving { class Score; }
namespace mscz::spelling {
// Internal, separately testable notation policy. TPCs, never MIDI pitches,
// are optimized. Original means the mechanically transposed source spelling.
int readableKey(int fifths);
struct Event {
    int pitch = 60;
    int original = mu::engraving::TPC_C;
    int key = 0;
    int harmonic = mu::engraving::TPC_INVALID;
    int tick = 0;
    int duration = 480;
    int measure = 0;
    bool approach = false;
    bool locked = false;
    bool essential = false; // explicit functional evidence beyond chord membership
};
struct Solution {
    int cost = 0;
    std::vector<int> tpcs;
};
Solution optimize(const std::vector<Event>& events);

// Capture before the existing transpose operation, apply afterwards. Keeps
// the original spellings available even when that operation simplifies them.
class TranspositionSpelling {
public:
    TranspositionSpelling(mu::engraving::Score* score, int fifths);
    ~TranspositionSpelling();
    void apply(bool readable);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
}
