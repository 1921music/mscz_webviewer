// SPDX-License-Identifier: GPL-3.0-only
#include "displaytranspose.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include "engraving/dom/chord.h"
#include "engraving/dom/chordlist.h"
#include "engraving/dom/fret.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/keylist.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tie.h"

using namespace mu::engraving;
namespace mscz::spelling {
int readableKey(int fifths)
{
    while (fifths > 5) { fifths -= 12; }
    while (fifths < -6) { fifths += 12; }
    return fifths;
}
namespace {
int pc(int pitch) { return (pitch % 12 + 12) % 12; }
bool uncommonSingle(int tpc)
{
    return tpc == TPC_C_B || tpc == TPC_F_B || tpc == TPC_E_S || tpc == TPC_B_S;
}
bool difficultSpelling(int tpc)
{
    return uncommonSingle(tpc) || std::abs(int(tpc2alter(tpc))) > 1;
}
// Walk the domain model rather than the visible layout: hidden staves,
// grace notes and harmonies inside fret diagrams also need correct spelling.
void forEachElement(Score* score, const std::function<void(EngravingItem*)>& visit)
{
    std::function<void(EngravingItem*)> walk = [&](EngravingItem* e) {
        if (!e) { return; }
        visit(e);
        if (e->isChord()) {
            auto chord = toChord(e);
            for (auto n : chord->notes()) { visit(n); }
            for (auto grace : chord->graceNotes()) { walk(grace); }
        } else if (e->isFretDiagram()) {
            if (auto harmony = toFretDiagram(e)->harmony()) { visit(harmony); }
        }
    };
    for (auto s = score->firstSegment(SegmentType::All); s; s = s->next1()) {
        for (auto e : s->elist()) { walk(e); }
        for (auto e : s->annotations()) { walk(e); }
    }
}
std::vector<int> candidates(int original)
{
    std::vector<int> result { original };
    for (int tpc = TPC_F_BB; tpc <= TPC_B_SS; ++tpc) {
        if (tpc != original && pc(tpc2pitch(tpc)) == pc(tpc2pitch(original))) {
            result.push_back(tpc);
        }
    }
    return result;
}
int noteCost(const Event& e, int tpc)
{
    int cost = tpc == e.original ? 0 : 4; // modest protection of the source
    if (e.harmonic != TPC_INVALID && tpc != e.harmonic) {
        // Chord membership alone does not require Cb/Fb/E#/B# in a jazz
        // melody. Explicit functional evidence (e.g. a resolving leading
        // tone) still protects the spelling, just as for double accidentals.
        cost += e.essential ? 100 : e.approach ? 1 : uncommonSingle(e.harmonic) ? 0 : 100;
    }
    const int alter = std::abs(int(tpc2alter(tpc)));
    if (alter > 1) { cost += (e.approach ? 40 : 12) * (alter - 1); }
    if (uncommonSingle(tpc)) { cost += e.approach ? 40 : 12; }
    // Chromatic notes are legitimate. Penalize a less suitable spelling,
    // rather than penalizing the fact that the source used a chromatic note.
    const Key key = Key(e.key);
    cost += 15 * std::max(0, std::abs(tpc2alterByKey(tpc, key))
                            - std::abs(tpc2alterByKey(e.original, key)));
    if (tpc2alterByKey(tpc, key) != 0) { cost += 3; }
    if (tpc == TPC_G_B || tpc == TPC_A_S) { ++cost; }
    return cost;
}
int transition(const Event& a, int ta, const Event& b, int tb)
{
    if (b.tick > a.tick + a.duration) { return 0; } // rest / phrase boundary
    int cost = 0;
    const int distance = b.pitch - a.pitch;
    const int sa = playingOctave(a.pitch, ta) * 7 + tpc2step(ta);
    const int sb = playingOctave(b.pitch, tb) * 7 + tpc2step(tb);
    const int expected = playingOctave(b.pitch, b.original) * 7 + tpc2step(b.original)
                       - playingOctave(a.pitch, a.original) * 7 - tpc2step(a.original);
    if (std::abs(distance) == 1) {
        if ((distance > 0 && (sb < sa || sb - sa > 1))
            || (distance < 0 && (sb > sa || sa - sb > 1))) { cost += 8; }
        if (sb - sa != expected) { cost += 2; }
    }
    if (a.measure == b.measure && sa == sb && tpc2alter(ta) != tpc2alter(tb)) { cost += 5; }
    // Same sounding note keeps its spelling unless the source itself changed
    // its role. The pair cost also counts actual accidental state changes.
    if (distance == 0 && a.original == b.original && ta != tb) { cost += 8; }
    if (a.measure == b.measure && sa == sb && ta == tb) { cost -= 3; }
    return cost;
}
}
Solution optimize(const std::vector<Event>& events)
{
    Solution result;
    if (events.empty()) { return result; }
    std::vector<std::vector<int>> choices, costs, previous;
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        choices.push_back(e.locked ? std::vector<int>{e.original} : candidates(e.original));
        costs.emplace_back(choices.back().size(), std::numeric_limits<int>::max() / 2);
        previous.emplace_back(choices.back().size(), -1);
        for (size_t c = 0; c < choices[i].size(); ++c) {
            const int own = noteCost(e, choices[i][c]);
            if (!i) { costs[i][c] = own; continue; }
            for (size_t p = 0; p < choices[i - 1].size(); ++p) {
                const int cost = costs[i - 1][p] + own
                               + transition(events[i - 1], choices[i - 1][p], e, choices[i][c]);
                if (cost < costs[i][c]) { costs[i][c] = cost; previous[i][c] = int(p); }
            }
        }
    }
    int c = int(std::min_element(costs.back().begin(), costs.back().end()) - costs.back().begin());
    result.cost = costs.back()[c];
    result.tpcs.resize(events.size());
    for (int i = int(events.size()) - 1; i >= 0; --i) {
        result.tpcs[i] = choices[i][c];
        c = previous[i][c];
    }
    return result;
}

class TranspositionSpelling::Impl {
public:
    struct NoteData {
        Note* note;
        int concert, written, keyConcert, keyWritten;
        int sourceDisplayed;
        bool leading;
        bool zeroConcert, zeroWritten;
    };
    struct HarmonyData {
        Harmony* harmony;
        HarmonyInfo* info;
        int root, bass, key, mask;
        int sourceRoot;
        bool zeroShift;
    };
    Score* score;
    int shift;
    std::map<Staff*, KeyList> keys;
    std::vector<NoteData> notes;
    std::vector<HarmonyData> harmonies;
    Impl(Score* s, int fifths) : score(s), shift(fifths)
    {
        for (auto staff : score->staves()) { keys.emplace(staff, *staff->keyList()); }
        std::set<EngravingItem*> seen;
        forEachElement(score, [&](EngravingItem* e) {
            if (!seen.insert(e).second || !e->staff() || !e->staff()->isPitchedStaff(e->tick())) { return; }
            const auto event = keys.at(e->staff()).key(e->tick().ticks());
            const int kc = normalized(event, true), kw = normalized(event, false);
            const int dc = delta(event, true), dw = delta(event, false);
            if (e->isNote()) {
                auto n = toNote(e);
                const int displayed = n->concertPitch() ? n->tpc1() : n->tpc2();
                const Key oldKey = n->concertPitch() ? event.concertKey() : event.key();
                notes.push_back({ n, shifted(n->tpc1(), dc), shifted(n->tpc2(), dw), kc, kw,
                                  displayed, event.mode() == KeyMode::MINOR
                                  && displayed == key2Tpc(oldKey) + 8, dc == 0, dw == 0 });
            } else if (e->isHarmony()) {
                auto h = toHarmony(e);
                for (auto info : h->chords()) {
                    const auto desc = info->descr();
                    const auto parsed = info->parsedChord();
                    const int mask = parsed && parsed->understandable() ? parsed->keys()
                                   : desc ? desc->chord.getKeys() : 0;
                    const int d = h->concertPitch() ? dc : dw;
                    harmonies.push_back({ h, info, shifted(info->rootTpc(), d), shifted(info->bassTpc(), d),
                                          h->concertPitch() ? kc : kw, mask, info->rootTpc(), d == 0 });
                }
            }
        });
        std::stable_sort(notes.begin(), notes.end(), [](const NoteData& a, const NoteData& b) {
            if (a.note->track() != b.note->track()) { return a.note->track() < b.note->track(); }
            if (a.note->tick() != b.note->tick()) { return a.note->tick() < b.note->tick(); }
            return a.note->pitch() < b.note->pitch();
        });
        std::stable_sort(harmonies.begin(), harmonies.end(), [](const HarmonyData& a, const HarmonyData& b) {
            return a.harmony->tick() < b.harmony->tick();
        });
    }
    static int shifted(int tpc, int d) {
        return tpcIsValid(tpc) ? clampEnharmonic(tpc + d, true) : tpc;
    }
    int normalized(const KeySigEvent& e, bool concert) const {
        const int k = int(concert ? e.concertKey() : e.key());
        return !e.isValid() || e.isAtonal() || e.custom() ? 0 : readableKey(k + shift);
    }
    int delta(const KeySigEvent& e, bool concert) const {
        if (!e.isValid() || e.isAtonal() || e.custom()) { return shift; }
        return normalized(e, concert) - int(concert ? e.concertKey() : e.key());
    }
    void normalizeKeys() {
        // The existing transpose created any missing initial signatures.
        // Retain mode/custom definitions and synchronize both the staff key
        // timeline and the actual signatures, including generated courtesies.
        std::vector<KeySig*> signatures;
        std::set<KeySig*> seen;
        auto collect = [&](EngravingItem* e) {
            if (e->isKeySig() && seen.insert(toKeySig(e)).second) { signatures.push_back(toKeySig(e)); }
        };
        forEachElement(score, collect);
        score->scanElements(collect); // generated courtesy signatures
        for (auto staff : score->staves()) {
            if (!staff->isPitchedStaff(Fraction(0, 1))) { continue; }
            auto original = keys.at(staff);
            original.setKey(0, keys.at(staff).key(0));
            for (const auto& [tick, old] : original) {
                if (!old.isValid() || old.isAtonal() || old.custom()) { continue; }
                auto event = old;
                event.setConcertKey(Key(normalized(old, true)));
                event.setKey(Key(normalized(old, false)));
                staff->setKey(Fraction::fromTicks(tick), event);
            }
        }
        for (auto ks : signatures) {
            if (!ks->staff() || !ks->staff()->isPitchedStaff(ks->tick())) { continue; }
            auto old = keys.at(ks->staff()).key(ks->tick().ticks());
            if (!old.isValid() || old.isAtonal() || old.custom()) { continue; }
            auto event = ks->keySigEvent();
            event.setConcertKey(Key(normalized(old, true)));
            event.setKey(Key(normalized(old, false)));
            ks->setKeySigEvent(event);
        }
    }
    HarmonyData* active(const NoteData& n) {
        HarmonyData* result = nullptr;
        for (auto& h : harmonies) {
            if (h.harmony->tick() > n.note->tick()) { break; }
            if (h.harmony->staff() == n.note->staff()) { result = &h; }
        }
        return result;
    }
    void apply(bool readable) {
        normalizeKeys();
        // Preserve mode retains the engine's pre-existing double-accidental
        // simplification, but uses the interval implied by the selected key.
        for (auto& n : notes) {
            score->undoChangePitch(n.note, n.note->pitch(), n.zeroConcert ? n.concert : clampEnharmonic(n.concert, false),
                                   n.zeroWritten ? n.written : clampEnharmonic(n.written, false));
        }
        for (auto& h : harmonies) {
            h.info->setRootTpc(tpcIsValid(h.root) && !h.zeroShift ? clampEnharmonic(h.root, false) : h.root);
            h.info->setBassTpc(tpcIsValid(h.bass) && !h.zeroShift ? clampEnharmonic(h.bass, false) : h.bass);
        }
        if (readable) { optimizeMusic(); }
        // A tie must keep one spelling even across an enharmonic key change.
        for (auto& n : notes) {
            if (auto tie = n.note->tieBackNonPartial()) {
                if (auto start = tie->startNote()) {
                    score->undoChangePitch(n.note, n.note->pitch(), start->tpc1(), start->tpc2());
                }
            }
        }
        for (auto& h : harmonies) {
            h.harmony->realizedHarmony().setDirty(true);
            h.harmony->setXmlText(h.harmony->harmonyName());
            h.harmony->triggerLayout();
        }
    }
    void optimizeMusic();
};

void TranspositionSpelling::Impl::optimizeMusic()
{
    // A hierarchy of chord candidates and voice-sequence DP. Chord changes
    // are conservative: preserve source root intervals and protect diatonic
    // functions. Each candidate is evaluated together with its melody block.
    std::map<HarmonyData*, std::vector<NoteData*>> blocks;
    for (auto& n : notes) { blocks[active(n)].push_back(&n); }
    std::vector<HarmonyData*> order { nullptr };
    for (auto& h : harmonies) { order.push_back(&h); }
    for (auto h : order) {
        auto& block = blocks[h];
        int bestCost = std::numeric_limits<int>::max();
        std::vector<int> bestNotes;
        int bestRoot = h ? h->info->rootTpc() : TPC_INVALID;
        const auto roots = h && tpcIsValid(h->root) ? candidates(h->root) : std::vector<int>{TPC_INVALID};
        for (int root : roots) {
            int cost = 0;
            if (h && tpcIsValid(root)) {
                Event e; e.original = h->root; e.key = h->key;
                cost = noteCost(e, root);
                if (root != h->root && tpc2alterByKey(h->root, Key(h->key)) == 0) { cost += 100; }
                if (std::abs(int(tpc2alter(root))) > 1) { cost += 40; }
                // Retain the source's fifth / fourth relationships with the
                // preceding chord. Do not infer a new harmonic analysis.
                auto it = std::find_if(harmonies.begin(), harmonies.end(), [&](const auto& prev) { return &prev == h; });
                while (it != harmonies.begin() && it != harmonies.end()) {
                    --it;
                    if (it->harmony->staff() != h->harmony->staff()) { continue; }
                    if (tpcIsValid(it->root) && (root - it->info->rootTpc() != h->root - it->root)) { cost += 8; }
                    break;
                }
            }
            std::vector<int> chosen;
            for (size_t start = 0; start < block.size();) {
                size_t end = start + 1;
                while (end < block.size() && block[end]->note->track() == block[start]->note->track()) { ++end; }
                std::vector<Event> events;
                for (size_t i = start; i < end; ++i) {
                    const auto& n = *block[i];
                    const bool concert = n.note->concertPitch();
                    Event e;
                    e.pitch = n.note->epitch();
                    e.original = concert ? n.concert : n.written;
                    e.key = concert ? n.keyConcert : n.keyWritten;
                    e.tick = n.note->tick().ticks();
                    e.duration = n.note->chord()->actualTicks().ticks();
                    e.measure = n.note->chord()->measure()->tick().ticks();
                    // Keep ordinary chord/tie spellings conservative, but
                    // allow difficult spellings to simplify. Tie continuations
                    // are synchronized with their head after optimization.
                    e.locked = !difficultSpelling(e.original)
                               && (n.note->tieBack() || n.note->tieFor() || n.note->chord()->notes().size() > 1);
                    if (i + 1 < end && e.duration <= 240 && e.tick % 480 != 0) {
                        const auto next = block[i + 1]->note;
                        e.approach = !e.locked && std::abs(next->epitch() - e.pitch) == 1
                                     && next->tick().ticks() == e.tick + e.duration
                                     && next->chord()->actualTicks().ticks() >= e.duration;
                    }
                    if (h && tpcIsValid(root) && tpcIsValid(h->sourceRoot)
                        && (h->mask & (1 << pc(tpc2pitch(n.sourceDisplayed) - tpc2pitch(h->sourceRoot))))) {
                        e.harmonic = shifted(e.original, root - h->root);
                    }
                    size_t resolution = i + 1;
                    while (resolution < end && block[resolution]->note->tieBack()
                           && block[resolution]->note->epitch() == e.pitch) { ++resolution; }
                    if (n.leading && resolution < end && block[resolution]->note->epitch() == e.pitch + 1) {
                        // An explicitly minor key plus the source's written
                        // leading tone resolving to its tonic is strong
                        // evidence even without a chord symbol.
                        e.harmonic = e.original;
                        e.essential = true;
                        e.approach = false;
                    }
                    events.push_back(e);
                }
                auto solution = optimize(events);
                cost += solution.cost;
                chosen.insert(chosen.end(), solution.tpcs.begin(), solution.tpcs.end());
                start = end;
            }
            if (cost < bestCost) { bestCost = cost; bestNotes = std::move(chosen); bestRoot = root; }
        }
        if (h && tpcIsValid(bestRoot)) {
            h->info->setRootTpc(bestRoot);
            if (tpcIsValid(h->bass)) {
                // Keep chord/bass interval spelling together; simplify an
                // otherwise unreadable double accidental in the bass.
                int bass = clampEnharmonic(shifted(h->bass, bestRoot - h->root), false);
                if (uncommonSingle(bass)) {
                    Event e; e.original = bass; e.key = h->key;
                    bass = optimize({e}).tpcs.front();
                }
                h->info->setBassTpc(bass);
            }
        }
        for (size_t i = 0; i < block.size(); ++i) {
            auto& n = *block[i];
            int c = n.concert, w = n.written;
            const int base = n.note->concertPitch() ? c : w;
            const int d = bestNotes[i] - base;
            c = shifted(c, d); w = shifted(w, d);
            score->undoChangePitch(n.note, n.note->pitch(), c, w);
        }
    }
}
TranspositionSpelling::TranspositionSpelling(Score* score, int fifths)
    : m_impl(std::make_unique<Impl>(score, fifths)) {}
TranspositionSpelling::~TranspositionSpelling() = default;
void TranspositionSpelling::apply(bool readable) { m_impl->apply(readable); }
}
