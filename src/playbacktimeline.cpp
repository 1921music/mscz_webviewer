// SPDX-License-Identifier: GPL-3.0-only
// Uses MuseScore's existing compatibility MIDI renderer. No second score parser.
#include "playbacktimeline.h"
#include <algorithm>
#include <cmath>
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
    json << "]}"; out = json.str(); return true;
}
}
