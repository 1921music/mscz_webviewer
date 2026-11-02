// SPDX-License-Identifier: GPL-3.0-only
#include "musicxmlimport.h"

#include <algorithm>
#include <cctype>
#include "serialization/zipreader.h"
#include "serialization/xmlstreamreader.h"
#include "engraving/dom/masterscore.h"
#include "importexport/musicxml/internal/import/importmusicxml.h"

namespace mscz {
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
    return mu::engraving::make_ret(mu::iex::musicxml::importMusicXmlfromBuffer(
        score, muse::String::fromStdString(info.name), data));
}
}
