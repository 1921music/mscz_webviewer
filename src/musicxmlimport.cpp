// SPDX-License-Identifier: GPL-3.0-only
#include "musicxmlimport.h"

#include <algorithm>
#include <cctype>
#include <map>
#include "serialization/zipreader.h"
#include "serialization/xmlstreamreader.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/symbol.h"
#include "engraving/dom/tempotext.h"
#include "engraving/style/style.h"
#include "engraving/types/symnames.h"
#include "importexport/musicxml/internal/import/importmusicxml.h"

namespace mscz {
namespace {
muse::String initialMeasureNumbering(const muse::ByteArray& data)
{
    muse::XmlStreamReader xml(data);
    // The first part's first measure can specify a score-wide numbering mode.
    const auto findChild = [&xml](const char* name) {
        while (xml.readNextStartElement()) {
            if (xml.name() == name) {
                return true;
            }
            xml.skipCurrentElement();
        }
        return false;
    };
    if (xml.readNextStartElement() && xml.name() == "score-partwise"
        && findChild("part") && findChild("measure") && findChild("print")
        && findChild("measure-numbering")) {
        return xml.readText().trimmed();
    }
    return {};
}

bool isLilyPondPrototype(muse::XmlStreamReader& xml)
{
    bool found = false;
    while (xml.readNextStartElement()) {
        if (xml.name() == "encoding") {
            while (xml.readNextStartElement()) {
                if (xml.name() == "software") {
                    if (xml.readText().trimmed() == u"mscz_webviewer LilyPond prototype") {
                        found = true;
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    return found;
}

void applyChordParenthesis(muse::XmlStreamReader& xml, mu::engraving::Measure* measure,
                           const mu::engraving::Fraction& tick, int divisions)
{
    using namespace mu::engraving;
    int offset = 0;
    ParenthesesMode mode = ParenthesesMode::NONE;
    while (xml.readNextStartElement()) {
        if (xml.name() == "offset") {
            offset = xml.readInt();
        } else if (xml.name() == "level") {
            const auto level = xml.readText();
            if (level == u"openbook-parenthesis-left") {
                mode = ParenthesesMode::LEFT;
            } else if (level == u"openbook-parenthesis-right") {
                mode = ParenthesesMode::RIGHT;
            } else if (level == u"openbook-parenthesis-both") {
                mode = ParenthesesMode::BOTH;
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    if (mode == ParenthesesMode::NONE) {
        return;
    }
    auto* segment = measure->findSegment(SegmentType::ChordRest, tick + Fraction(offset, 4 * divisions));
    if (!segment) {
        return;
    }
    for (auto* annotation : segment->annotations()) {
        if (annotation->isHarmony()) {
            annotation->setParenthesesMode(mode, false);
            break;
        }
    }
}

void applyDirectionPosition(muse::XmlStreamReader& xml, mu::engraving::Segment* segment)
{
    using namespace mu::engraving;
    while (xml.readNextStartElement()) {
        if (xml.name() != "direction-type") {
            xml.skipCurrentElement();
            continue;
        }
        while (xml.readNextStartElement()) {
            if (xml.name() == "other-direction" && segment && xml.asciiAttribute("smufl") != "segno") {
                const auto name = xml.asciiAttribute("smufl");
                const auto sym = SymNames::symIdByName(name, SymId::noSym);
                const double x = xml.doubleAttribute("relative-x") / 10;
                const double y = xml.doubleAttribute("default-y") / 10;
                for (auto* annotation : segment->annotations()) {
                    if (annotation->isSymbol() && toSymbol(annotation)->sym() == sym) {
                        const double space = annotation->spatium();
                        annotation->setOffset(PointF(x * space, -y * space));
                        annotation->setPropertyFlags(Pid::OFFSET, PropertyFlags::UNSTYLED);
                        break;
                    }
                }
            }
            xml.skipCurrentElement();
        }
    }
}

// Enable the staff skyline only for the prototype's imported Segno symbols.
void enableImportedSegnoAutoplace(mu::engraving::MasterScore* score)
{
    using namespace mu::engraving;
    for (auto* segment = score->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        for (auto* annotation : segment->annotations()) {
            if (annotation->isSymbol() && toSymbol(annotation)->sym() == SymId::segno) {
                toSymbol(annotation)->setAutoplaceAsDirection(true);
                annotation->setMinDistance(score->style().styleS(Sid::staffTextMinDistance));
            }
        }
    }
}

int applyLyricExtents(muse::XmlStreamReader& xml, mu::engraving::Segment* segment,
                      std::map<int, mu::engraving::Lyrics*>& openExtenders)
{
    using namespace mu::engraving;
    int duration = 0;
    while (xml.readNextStartElement()) {
        if (xml.name() == "duration") {
            duration = xml.readInt();
            continue;
        }
        if (xml.name() != "lyric") {
            xml.skipCurrentElement();
            continue;
        }
        const int number = xml.intAttribute("number") - 1;
        while (xml.readNextStartElement()) {
            if (xml.name() == "extend" && segment && number >= 0) {
                const auto type = xml.asciiAttribute("type");
                if (type == "stop") {
                    const auto it = openExtenders.find(number);
                    if (it != openExtenders.end()) {
                        auto* lyric = it->second;
                        lyric->setTicks(segment->tick() - lyric->segment()->tick());
                        openExtenders.erase(it);
                    }
                } else if (type == "start") {
                    auto* item = segment->element(0);
                    if (item && item->isChordRest()) {
                        for (auto* lyric : toChordRest(item)->lyrics()) {
                            if (lyric->verse() == number) {
                                openExtenders[number] = lyric;
                                break;
                            }
                        }
                    }
                }
            }
            xml.skipCurrentElement();
        }
    }
    return duration;
}

bool applyLilyPondEngraving(mu::engraving::MasterScore* score, const muse::ByteArray& data)
{
    using namespace mu::engraving;
    if (score->nstaves() != 1) {
        return false;
    }
    muse::XmlStreamReader xml(data);
    if (!xml.readNextStartElement() || xml.name() != "score-partwise") {
        return false;
    }
    bool lilyPondPrototype = false;
    while (xml.readNextStartElement()) {
        if (xml.name() == "identification") {
            lilyPondPrototype = isLilyPondPrototype(xml);
        } else if (xml.name() == "part") {
            break;
        } else {
            xml.skipCurrentElement();
        }
    }
    if (!lilyPondPrototype || xml.name() != "part") {
        return false;
    }
    enableImportedSegnoAutoplace(score);
    auto* measure = score->firstMeasure();
    int divisions = 1;
    std::map<int, Lyrics*> openExtenders;
    while (measure && xml.readNextStartElement()) {
        if (xml.name() != "measure") {
            xml.skipCurrentElement();
            continue;
        }
        auto tick = measure->tick();
        while (xml.readNextStartElement()) {
            if (xml.name() == "print") {
                auto* previous = measure->prevMeasure();
                if (previous && xml.asciiAttribute("new-system") == "no" && !previous->pageBreak()
                    && !previous->lineBreak() && !previous->sectionBreak()) {
                    previous->setNoBreak(true);
                }
                xml.skipCurrentElement();
            } else if (xml.name() == "attributes") {
                while (xml.readNextStartElement()) {
                    if (xml.name() == "divisions") {
                        divisions = std::max(1, xml.readInt());
                    } else {
                        xml.skipCurrentElement();
                    }
                }
            } else if (xml.name() == "note") {
                const int duration = applyLyricExtents(xml, measure->findSegment(SegmentType::ChordRest, tick), openExtenders);
                tick += Fraction(duration, 4 * divisions);
            } else if (xml.name() == "harmony") {
                applyChordParenthesis(xml, measure, tick, divisions);
            } else if (xml.name() == "direction") {
                applyDirectionPosition(xml, measure->findSegment(SegmentType::ChordRest, tick));
            } else {
                xml.skipCurrentElement();
            }
        }
        measure = measure->nextMeasure();
    }
    return true;
}

void spaceParenthesizedMetronomeMarks(mu::engraving::MasterScore* score)
{
    using namespace mu::engraving;
    for (auto* segment = score->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        for (auto* annotation : segment->annotations()) {
            if (!annotation->isTempoText()) {
                continue;
            }
            auto* tempo = toTempoText(annotation);
            const auto& original = tempo->xmlText();
            // The music glyph and opening parenthesis otherwise touch in SVG.
            if (original.contains(u"(<sym>metNote")) {
                auto spaced = original;
                spaced.replace(u"(<sym>metNote", u"(\u2002<sym>metNote");
                tempo->setXmlText(spaced);
            }
        }
    }
}
}

bool isMusicXml(const ScoreInfo& info)
{
    const auto dot = info.name.find_last_of('.');
    auto suffix = dot == std::string::npos ? std::string() : info.name.substr(dot);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return std::tolower(c); });
    return suffix == ".mxl" || suffix == ".xml" || suffix == ".musicxml";
}

muse::Ret importMusicXml(mu::engraving::MasterScore* score, StreamDevice& device, const ScoreInfo& info)
{
    if (!device.open(muse::io::IODevice::ReadOnly)) {
        return muse::Ret(1, "Could not open the MusicXML stream.");
    }
    muse::ByteArray data;
    const auto header = device.read(4);
    device.seek(0);
    if (header.size() >= 2 && header.at(0) == 'P' && header.at(1) == 'K') {
        // Same META-INF/container.xml rootfile convention as MuseScore's
        // extractRootfile(), adapted to a device rather than a filesystem path.
        muse::ZipReader zip(&device);
        const auto container = zip.fileData("META-INF/container.xml");
        muse::XmlStreamReader xml(container);
        muse::String root;
        if (xml.readNextStartElement() && xml.name() == "container") {
            while (xml.readNextStartElement()) {
                if (xml.name() != "rootfiles") { xml.skipCurrentElement(); continue; }
                while (xml.readNextStartElement()) {
                    if (xml.name() == "rootfile" && root.empty()) {
                        root = xml.attribute("full-path");
                    }
                    xml.skipCurrentElement();
                }
            }
        }
        if (xml.isError() || root.empty() || !zip.fileExists(root.toStdString())) {
            return muse::Ret(1, "Invalid MXL container: score rootfile is missing or unreadable.");
        }
        data = zip.fileData(root.toStdString());
        if (zip.hasError()) { return muse::Ret(1, "Could not decompress the MXL score."); }
    } else {
        data = device.readAll();
    }
    if (data.empty()) { return muse::Ret(1, "The MusicXML score is empty."); }
    // Match the desktop import entry point's undo-load guard. The upstream
    // parser handles partwise MusicXML and returns an error for other roots.
    mu::engraving::ScoreLoad loading;
    const auto result = mu::engraving::make_ret(mu::iex::musicxml::importMusicXmlfromBuffer(
        score, muse::String::fromStdString(info.name), data));
    if (result && applyLilyPondEngraving(score, data)) {
        spaceParenthesizedMetronomeMarks(score);
        const auto mode = initialMeasureNumbering(data);
        using mu::engraving::Sid;
        if (mode == "none") {
            score->style().set(Sid::showMeasureNumber, false);
        } else if (mode == "system") {
            score->style().set(Sid::showMeasureNumber, true);
            score->style().set(Sid::measureNumberSystem, true);
        }
    }
    return result;
}
}
