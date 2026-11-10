// SPDX-License-Identifier: GPL-3.0-only
// Uses MuseScore's existing compatibility MIDI renderer. No second score parser.
#include "playbacktimeline.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>
#include "engraving/compat/midi/compatmidirender.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/tempo.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/system.h"
#include "engraving/dom/page.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"

namespace mscz {
namespace {
std::string quoted(const std::string& s) {
    std::ostringstream o; o << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o << '\\' << c;
        else if (c < 32) o << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << int(c) << std::dec;
        else o << c;
    }
    o << '"'; return o.str();
}
struct Event { double time; int part, channel, type, a, b; };
}
bool playbackTimeline(mu::engraving::MasterScore* score, std::string& out, std::string& error) {
    using namespace mu::engraving;
    if (!score || !score->firstMeasure()) { error = "The score has no measures."; return false; }
    for (auto* part : score->parts()) part->updateHarmonyChannels(false);
    score->rebuildMidiMapping();
    score->setExpandRepeats(true);
    // The display-only load path has no sequencer to refresh the unwound
    // segments' utick/utime fields. Do this before the MIDI renderer uses them.
    score->repeatList();
    score->updateRepeatListTempo();
    EventsHolder events;
    CompatMidiRendererInternal::Context context;
    context.applyCaesuras = true;
    context.harmonyChannelSetting = CompatMidiRendererInternal::DEFAULT;
    // GM does not implement MuseScore's sound-specific breath-controller dynamics.
    context.sndController = CTRL_EXPRESSION;
    CompatMidiRender::renderScore(score, events, context, true);
    // Keep score parts first, then expose each harmony channel as an independent
    // mixer part. The worker already isolates synth state and controls per row.
    std::vector<std::pair<Part*, bool>> parts;
    for (auto* part : score->parts()) parts.emplace_back(part, false);
    for (auto* part : score->parts()) {
        if (part->harmonyChannel() && part->harmonyCount() > 0) parts.emplace_back(part, true);
    }
    if (parts.size() > 128) { error = "Playback supports at most 128 parts including chord accompaniment."; return false; }
    std::map<int, std::pair<int, int>> channels;
    std::vector<Event> timeline;
    std::ostringstream json; json << std::setprecision(12) << "{\"parts\":[";
    for (size_t p = 0; p < parts.size(); ++p) {
        if (p) json << ',';
        int next = 0, volume = 100, pan = 64;
        bool first = true;
        auto* part = parts[p].first;
        const bool harmony = parts[p].second;
        json << "{\"name\":" << quoted(part->partName().toStdString() + (harmony ? ": Chords" : ""))
             << ",\"kind\":" << quoted(harmony ? "harmony" : "instrument") << ",\"channels\":[";
        for (size_t i = 0; i < score->midiMapping().size(); ++i) {
            auto& mapping = score->midiMapping()[i];
            if (mapping.part() != part) continue;
            const auto* ch = mapping.articulation();
            if (ch->isHarmonyChannel() != harmony) continue;
            // Reserve channel 10 for GM percussion within each independent part.
            const bool drum = !harmony && mapping.channel() == 9;
            if (!drum && next == 9) ++next;
            int local = drum ? 9 : next++;
            if (local > 15) { error = "A part has too many simultaneous instrument channels."; return false; }
            channels[int(i)] = {int(p), local};
            if (!first) json << ',';
            else { volume = ch->volume(); pan = ch->pan(); }
            first = false;
            json << "[" << local << ',' << std::clamp(ch->program(), 0, 127) << ',' << int(ch->volume()) << ',' << int(ch->pan()) << "]";
        }
        json << "],\"volume\":" << volume << ",\"pan\":" << pan << '}';
    }
    // PauseMap copies tempo events with their original cached absolute times.
    // Recompute those caches after unfolding repeats before converting to seconds.
    TempoMap playbackTempo = *context.pauseMap->tempomapWithPauses();
    if (!playbackTempo.empty()) playbackTempo.setTempo(playbackTempo.begin()->first, playbackTempo.begin()->second.tempo);
    auto seconds = [&](int tick) {
        return playbackTempo.tick2time(context.pauseMap->tickWithPauses(std::max(0, tick)));
    };
    for (size_t i = 0; i < events.size(); ++i) {
        for (const auto& item : events[i]) {
            const auto& e = item.second;
            auto channel = channels.find(e.channel());
            if (channel == channels.end()) continue;
            int type = e.type(), a = e.dataA(), b = e.dataB();
            // EventsHolder already resolves duplicate notes; omitted note-offs
            // belong to notes kept alive by another staff in the same part.
            if (e.discard() && type == ME_NOTEON && e.velo() == 0) continue;
            if (type == ME_CONTROLLER && a == CTRL_PROGRAM) { type = 0xc0; a = b; b = 0; }
            if (type != ME_NOTEON && type != ME_NOTEOFF && type != ME_CONTROLLER && type != ME_PITCHBEND && type != 0xc0) continue;
            if (a < 0 || a > 127 || b < 0 || b > 127) continue;
            double time = seconds(item.first);
            if (!std::isfinite(time) || time < 0) continue;
            if (e.discard() && type == ME_NOTEON && e.velo() > 0) {
                timeline.push_back({time, channel->second.first, channel->second.second, ME_NOTEON, a, 0});
            }
            timeline.push_back({time, channel->second.first, channel->second.second, type, a, b});
        }
    }
    std::stable_sort(timeline.begin(), timeline.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
    double duration = seconds(score->repeatList().ticks());
    if (!timeline.empty()) duration = std::max(duration, timeline.back().time);
    json << "],\"duration\":" << duration << ",\"events\":[";
    bool first = true;
    for (const auto& e : timeline) {
        if (!first) json << ',';
        first = false;
        json << '[' << e.time << ',' << e.part << ',' << (e.type | e.channel) << ',' << e.a << ',' << e.b << ']';
    }
    // Display durations are notation durations, independent of articulation,
    // sustain and MIDI note deduplication. A tied continuation gets its own
    // interval, while rest-only measures still advance the system timeline.
    json << "],\"visual\":{\"version\":1,\"notes\":[";
    first = true;
    for (const auto* repeat : score->repeatList()) {
        for (auto* segment = score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest)) {
            const int tick = segment->tick().ticks();
            if (tick < repeat->tick || tick >= repeat->endTick()) continue;
            for (auto* element : segment->elist()) {
                if (!element || !element->isChord()) continue;
                const auto* chord = toChord(element);
                const int start = repeat->utick + tick - repeat->tick;
                const int end = std::min(repeat->utick + repeat->len(), start + chord->actualTicks().ticks());
                for (const auto* note : chord->notes()) {
                    if (!note->play()) continue;
                    if (!first) json << ',';
                    first = false;
                    json << "{\"id\":\"note_" << reinterpret_cast<uintptr_t>(note)
                         << "\",\"start\":" << seconds(start) << ",\"end\":" << seconds(end) << '}';
                }
            }
        }
    }
    // Grace chords have no written duration: retain their renderer timing.
    for (size_t channelIndex = 0; channelIndex < events.size(); ++channelIndex) {
        const auto& channelEvents = events[channelIndex];
        std::map<const Note*, double> starts;
        for (const auto& item : channelEvents) {
            const auto& e = item.second;
            const auto* note = e.note();
            if (!note || !note->chord()->isGrace() || e.discard()) continue;
            if (e.type() == ME_NOTEON && e.velo() > 0) starts[note] = seconds(item.first);
            else if (e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0)) {
                auto it = starts.find(note);
                if (it == starts.end()) continue;
                const double end = seconds(item.first);
                if (end <= it->second) { starts.erase(it); continue; }
                if (!first) json << ',';
                first = false;
                json << "{\"id\":\"note_" << reinterpret_cast<uintptr_t>(note)
                     << "\",\"start\":" << it->second << ",\"end\":" << end << '}';
                starts.erase(it);
            }
        }
    }
    json << "],\"systems\":[";
    first = true;
    for (const auto* repeat : score->repeatList()) {
        for (const auto* measure : repeat->measureList()) {
            const auto* displayed = measure;
            // Covered source measures do not own layout; follow their displayed
            // multi-measure rest without skipping the intervening playback time.
            while (displayed && displayed->mmRestCount() < 0) displayed = displayed->prevMeasure();
            if (displayed && displayed->mmRest()) displayed = displayed->mmRest();
            const auto* system = displayed ? displayed->system() : nullptr;
            if (!system || !system->page()) continue;
            const auto* page = system->page();
            const auto it = std::find(score->pages().begin(), score->pages().end(), page);
            const auto rect = page->ldata()->bbox();
            const auto pos = system->pagePos();
            const auto box = system->ldata()->bbox();
            const int start = repeat->utick + measure->tick().ticks() - repeat->tick;
            if (!first) json << ',';
            first = false;
            json << "{\"id\":\"system_" << reinterpret_cast<uintptr_t>(system)
                 << "\",\"page\":" << (it - score->pages().begin())
                 << ",\"start\":" << seconds(start)
                 << ",\"end\":" << seconds(start + measure->ticks().ticks())
                 << ",\"bounds\":[" << pos.x() / rect.width() << ',' << pos.y() / rect.height()
                 << ',' << box.width() / rect.width() << ',' << box.height() / rect.height() << "],\"anchors\":[";
            // Interpolate score-space positions, not note-on events. Include
            // tempo boundaries so long notes and rests follow tempo changes.
            std::vector<std::pair<int, double>> positions;
            for (auto* segment = displayed->first(SegmentType::ChordRest); segment; segment = segment->next(SegmentType::ChordRest)) {
                positions.emplace_back(segment->tick().ticks(), segment->pageX() / rect.width());
            }
            const int displayEnd = displayed->isMMRest() && displayed->mmRestLast()
                ? displayed->mmRestLast()->endTick().ticks() : displayed->endTick().ticks();
            double endX = (displayed->pagePos().x() + displayed->width()) / rect.width();
            const auto* next = displayed->nextMeasure();
            if (!displayed->isMMRest() && displayEnd < repeat->endTick() && next && next->system() == system) {
                if (const auto* segment = next->first(SegmentType::ChordRest)) endX = segment->pageX() / rect.width();
            }
            positions.emplace_back(displayEnd, endX);
            auto xAt = [&](int tick) {
                auto right = std::upper_bound(positions.begin(), positions.end(), tick,
                    [](int t, const auto& p) { return t < p.first; });
                if (right == positions.begin()) return right->second;
                const auto& left = *std::prev(right);
                if (right == positions.end()) return left.second;
                const double fraction = double(tick - left.first) / (right->first - left.first);
                return left.second + fraction * (right->second - left.second);
            };
            const int scoreStart = measure->tick().ticks(), scoreEnd = measure->endTick().ticks();
            std::map<int, double> anchors;
            anchors[scoreStart] = xAt(scoreStart);
            anchors[scoreEnd] = xAt(scoreEnd);
            for (const auto& p : positions) if (p.first > scoreStart && p.first < scoreEnd) anchors[p.first] = p.second;
            for (auto t = score->tempomap()->lower_bound(scoreStart); t != score->tempomap()->end() && t->first <= scoreEnd; ++t)
                anchors[t->first] = xAt(t->first);
            bool firstAnchor = true;
            auto point = [&](double time, double x) {
                if (!firstAnchor) json << ',';
                firstAnchor = false;
                json << '[' << time << ',' << x << ']';
            };
            for (const auto& anchor : anchors) {
                const double time = seconds(repeat->utick + anchor.first - repeat->tick);
                const double pause = score->tempomap()->pauseSecs(anchor.first);
                // Reach a pause's position before its silent hold, then stay there.
                if (pause > 0 && anchor.first > scoreStart) point(time - pause, anchor.second);
                point(time, anchor.second);
            }
            json << "]}";
        }
    }
    json << "]}}"; out = json.str(); return true;
}
}
