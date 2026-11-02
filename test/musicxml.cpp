// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <stdexcept>
#include "mscz_engine.h"
#include "global/io/buffer.h"
#include "global/serialization/zipwriter.h"
#include "global/serialization/zipreader.h"
#include "global/serialization/xmlstreamreader.h"

static void require(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error(message);
}
static std::unique_ptr<mscz::Session> open(const muse::ByteArray& bytes, const char* name, std::string& error)
{
    return mscz::Session::open(std::make_unique<mscz::BufferStream>(bytes.constVData()), {name}, error);
}
static muse::ByteArray zip(const muse::ByteArray& container, const muse::ByteArray& score)
{
    muse::ByteArray bytes;
    muse::io::Buffer device(&bytes);
    device.open(muse::io::IODevice::WriteOnly);
    muse::ZipWriter writer(&device);
    writer.addFile("META-INF/container.xml", container);
    writer.addFile("nested/score.musicxml", score);
    writer.close();
    require(!writer.hasError(), "test ZIP failed");
    return bytes;
}
static std::vector<std::string> fingerprint(mscz::Session& session)
{
    std::vector<std::string> lines;
    std::string error;
    require(session.layoutFingerprint(lines, error), error);
    return lines;
}
int main(int argc, char** argv)
{
    try {
        auto resources = mscz::makeCachingProvider(mscz::makeDirectoryProvider(MSCZ_RESOURCE_ROOT, MSCZ_RESOURCE_MANIFEST));
        mscz::Engine::setResourceProvider(resources);
        mscz::Engine::initializeIfNeeded();
        const muse::ByteArray xml(R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="4.0"><work><work-title>MusicXML import</work-title></work>
<part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
<part id="P1"><measure number="1"><attributes><divisions>1</divisions><key><fifths>0</fifths></key>
<time><beats>4</beats><beat-type>4</beat-type></time><clef><sign>G</sign><line>2</line></clef></attributes>
<harmony><root><root-step>C</root-step></root><kind>major</kind></harmony>
<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><type>whole</type></note>
</measure></part></score-partwise>)");
        const muse::ByteArray container(R"(<container><rootfiles><rootfile full-path="nested/score.musicxml"
media-type="application/vnd.recordare.musicxml+xml"/></rootfiles></container>)");
        std::string error;
        auto plain = open(xml, "fixture.musicxml", error);
        require(bool(plain), error);
        require(plain->pageCount() == 1, "minimal MusicXML should have one page");
        const auto original = fingerprint(*plain);
        for (const char* name : {"fixture.xml", "fixture.MUSICXML"}) {
            auto other = open(xml, name, error);
            require(bool(other), error);
            require(fingerprint(*other) == original, "XML extension changed layout");
        }
        auto compressed = open(zip(container, xml), "fixture.MXL", error);
        require(bool(compressed), error);
        require(fingerprint(*compressed) == original, "MXL and uncompressed XML layout differ");
        require(compressed->setViewTransposeSemitones(1, error), error);
        require(fingerprint(*compressed) != original, "MXL transposition had no effect");
        require(compressed->setViewTransposeSemitones(0, error), error);
        require(fingerprint(*compressed) == original, "MXL reset failed to reopen original source");
        for (const auto& bytes : {muse::ByteArray(), muse::ByteArray("<unrelated/>"),
                                 muse::ByteArray("<score-timewise/>"), muse::ByteArray("not a zip"),
                                 zip(muse::ByteArray("<container/>"), xml),
                                 zip(muse::ByteArray("<container><rootfiles><rootfile full-path='missing.xml'/></rootfiles></container>"), xml)}) {
            error.clear();
            require(!open(bytes, "bad.mxl", error) && !error.empty(), "invalid MusicXML was accepted without an error");
        }
        // User samples: compare the MXL with its actual rootfile as plain XML.
        for (int i = 1; i < argc; ++i) {
            auto imported = mscz::Session::openPath(argv[i], error);
            require(bool(imported), error);
            mscz::StreamDevice device(resources->open(argv[i]));
            require(device.open(muse::io::IODevice::ReadOnly), "could not open sample");
            muse::ZipReader archive(&device);
            muse::XmlStreamReader manifest(archive.fileData("META-INF/container.xml"));
            std::string root;
            while (!manifest.atEnd()) {
                if (manifest.readNext() == muse::XmlStreamReader::StartElement && manifest.name() == "rootfile") {
                    root = manifest.attribute("full-path").toStdString(); break;
                }
            }
            auto raw = open(archive.fileData(root), "sample.musicxml", error);
            require(bool(raw), error);
            require(fingerprint(*raw) == fingerprint(*imported), "sample MXL/plain XML differ");
            std::cout << argv[i] << ": " << imported->pageCount() << " pages, compressed/plain parity OK\n";
        }
        std::cout << "MusicXML import, errors, transpose/reset: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
