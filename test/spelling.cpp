// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include "mscz_engine.h"
#include "displaytranspose.h"
#include "global/modularity/ioc.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/note.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/mscore.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/rw/mscloader.h"
#include "engraving/editing/transpose.h"
using namespace mu::engraving;
static void require(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
int main(int argc, char** argv)
{
    try {
        mscz::Engine::setResourceProvider(mscz::makeCachingProvider(
            mscz::makeDirectoryProvider(MSCZ_RESOURCE_ROOT, MSCZ_RESOURCE_MANIFEST)));
        mscz::Engine::initializeIfNeeded();
        for (int sample = 0; sample < (argc > 1 ? 2 : 1); ++sample) {
            for (bool readable : {false, true}) {
                std::unique_ptr<MasterScore> score(compat::ScoreAccess::createMasterScoreWithDefaultStyle(muse::modularity::globalCtx()));
                score->setFileInfoProvider(mscz::makeStreamFileInfoProvider({"spelling.mscx"}));
                MscReader::Params params;
                params.filePath = sample ? argv[1] : MSCZ_SPELLING_FIXTURE;
                if (!sample) { params.mainFileName = u"spelling.mscx"; }
                params.mode = sample ? MscIoMode::Zip : MscIoMode::Dir;
                MscReader reader(params);
                require(bool(reader.open()), "fixture could not open");
                MscLoader loader;
                require(bool(loader.loadMscz(score.get(), reader, nullptr, false)), "fixture could not load");
                score->setLayoutAll(); score->doLayout();
                std::vector<Note*> notes;
                std::vector<Harmony*> harmonies;
                std::set<EngravingItem*> seen;
                score->scanElements([&](EngravingItem* e) {
                    if (!seen.insert(e).second) { return; }
                    if (e->isNote()) { notes.push_back(toNote(e)); }
                    if (e->isHarmony()) { harmonies.push_back(toHarmony(e)); }
                });
                std::sort(notes.begin(), notes.end(), [](auto a, auto b) { return a->tick() < b->tick(); });
                std::sort(harmonies.begin(), harmonies.end(), [](auto a, auto b) { return a->tick() < b->tick(); });
                if (!sample && (notes.size() != 13 || harmonies.size() != 3)) {
                    std::cerr << "fixture: notes=" << notes.size() << " harmonies=" << harmonies.size() << '\n';
                }
                require(sample || (notes.size() == 13 && harmonies.size() == 3), "fixture contents missing");
                std::vector<int> pitches;
                for (auto n : notes) { pitches.push_back(n->pitch()); }
                mscz::spelling::TranspositionSpelling policy(score.get(), 5); // down minor second
                score->startCmd(muse::TranslatableString("mscz_test", "spelling regression"));
                score->cmdSelectAll();
                require(Transpose::transpose(score.get(), TransposeMode::BY_INTERVAL, TransposeDirection::DOWN,
                                             Key::C, 3, true, true, false), "fixture transposition failed");
                policy.apply(readable);
                score->endCmd();
                score->deselectAll(); score->setLayoutAll(); score->doLayout();
                if (!sample) {
                    require(int(score->staff(0)->concertKey(Fraction(0, 1))) == 5, "C down semitone must use B key");
                    if (int(score->staff(0)->concertKey(notes[4]->tick())) != -6) {
                        std::cerr << "second key=" << int(score->staff(0)->concertKey(notes[4]->tick()))
                                  << " tick=" << notes[4]->tick().ticks() << '\n';
                    }
                    require(int(score->staff(0)->concertKey(notes[4]->tick())) == -6, "G down semitone must use G flat key");
                    require(harmonies[0]->rootTpc() == TPC_D_S, "B key must retain D sharp 7, not E flat 7");
                    require(harmonies[1]->rootTpc() == TPC_G_B, "G flat key must use G flat 7");
                    require(notes[0]->tpc1() == (readable ? TPC_F_SS : TPC_G), "third spelling policy failed");
                    require(notes[2]->tpc1() == TPC_G, "approach must simplify double sharp");
                    require(notes[4]->tpc1() == TPC_B_B, "local key did not govern mechanically transposed note");
                    require(notes[5]->tpc1() == (readable ? TPC_F_SS : TPC_G), "explicit minor leading tone was mistaken for an approach");
                    require(harmonies[2]->rootTpc() == TPC_D_B, "Db minor 7 chord root was needlessly respelled");
                    require(harmonies[2]->bassTpc() == (readable ? TPC_E : TPC_F_B), "slash bass must simplify Fb to E in readable mode");
                    require(notes[7]->tpc1() == (readable ? TPC_B : TPC_C_B), "Dbm7 seventh must simplify Cb to B in readable mode");
                    require(notes[8]->tpc1() == (readable ? TPC_E : TPC_F_B), "Dbm7 third must simplify Fb to E in readable mode");
                    require(notes[9]->tpc1() == notes[8]->tpc1(), "simplified spelling differs across tie");
                    require(notes[10]->tpc1() == TPC_B_S && notes[11]->tpc1() == TPC_B_S,
                            "tied minor leading tone must retain B sharp");
                } else {
                    int targets = 0, chords = 0;
                    for (auto n : notes) {
                        const int measure = n->chord()->measure()->no();
                        if (measure != 5 && measure != 7) { continue; }
                        const int expected = n->pitch() == 71 ? (readable ? TPC_B : TPC_C_B)
                                           : n->pitch() == 64 ? (readable ? TPC_E : TPC_F_B) : TPC_INVALID;
                        require(expected != TPC_INVALID && n->tpc1() == expected, "Those Days second system must read B natural / E natural");
                        ++targets;
                    }
                    for (auto h : harmonies) {
                        if (h->tick() == Fraction(5, 1) || h->tick() == Fraction(7, 1)) {
                            require(h->rootTpc() == TPC_D_B, "Those Days Dbm7 root changed");
                            ++chords;
                        }
                    }
                    require(targets == 6 && chords == 2, "Those Days regression targets missing");
                }
                for (size_t i = 0; i < notes.size(); ++i) {
                    require(notes[i]->pitch() == pitches[i] - 1, "spelling optimizer changed sounding pitch");
                }
                std::cout << "PASS " << (sample ? "Those Days " : "fixture ") << (readable ? "readable" : "preserve")
                          << (sample ? ": Dbm7, B/E naturals, ties, unchanged pitches\n"
                                     : ": B/D#7, Gb modulation, Dbm7, approaches and leading tones, unchanged pitches\n");
            }
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
