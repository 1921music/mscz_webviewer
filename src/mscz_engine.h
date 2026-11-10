/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "hostio.h"

//! The slim display-only facade over the vendored MuseScore engine: open a
//! score, lay it out, transpose or switch parts for display, paint a page.
//!
//! Deliberately not aria's ScoreRenderSession. That one is a single
//! translation unit covering editing, playback, MIDI and MusicXML, so it
//! cannot be taken in pieces. The display logic here is ported from its
//! score_render_runtime / open_save / pages parts.
namespace mscz {

struct PageImage {
    int pageIndex = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    //! PNG bytes.
    std::vector<unsigned char> data;
};

class Engine
{
public:
    //! Starts the engine. Safe to call more than once; later calls do nothing.
    //! Must be the first thing called, on the main thread.
    //!
    //! `resources` is how the engine gets at fonts, the SMuFL metadata and the
    //! instrument definitions -- by the ":/..." names it uses internally. It
    //! is the only route to a file: nothing in the engine opens one itself,
    //! which is what lets the same code run in a browser with a provider that
    //! fetches instead. Must be set before the first call, and is ignored
    //! afterwards.
    static void setResourceProvider(std::shared_ptr<IResourceProvider> resources);

    static void initializeIfNeeded();
};

class Session
{
public:
    //! Opens MSCZ or MusicXML (.musicxml/.xml/.mxl, selected by info.name)
    //! from a seekable reader. The reader is handed to the ZIP
    //! layer as a device, so a .mscz is not pulled in whole: it seeks to the
    //! central directory and inflates only the entries that get read.
    static std::unique_ptr<Session> open(std::unique_ptr<IReadStream> score, const ScoreInfo& info,
                                        std::string& error);

    //! Convenience for a host that has a path: opens it through the resource
    //! provider, so the engine still never sees a filesystem.
    static std::unique_ptr<Session> openPath(const std::string& scorePath, std::string& error);

    ~Session();

    int pageCount() const;
    bool rostrumInfo(std::string& out, std::string& error, bool hideAnnotations = true);
    bool saveRostrum(const std::string& data, std::string& out, std::string& error) const;
#ifdef MSCZ_ENABLE_PLAYBACK
    bool playbackTimeline(std::string& out, std::string& error);
#endif

    //! Absolute display transposition relative to the opened score, in
    //! semitones (-12..12). Keys and chord names follow pitched notes;
    //! percussion stays unchanged. The read-only source is never modified.
    //! Failure leaves the current view intact; zero restores the original.
    int viewTransposeSemitones() const;
    bool setViewTransposeSemitones(int semitones, std::string& error);
    //! Both modes choose standard keys with at most six accidentals (Gb at
    //! the tie). Preserve retains the engine's existing double-accidental
    //! simplification; Readable additionally uses contextual spelling costs.
    //! Mode switches also start from the original source. At zero, neither
    //! policy changes the source notation.
    enum class TransposeSpelling { Preserve = 0, Readable = 1 };
    TransposeSpelling viewTransposeSpelling() const;
    bool setViewTranspose(int semitones, TransposeSpelling spelling, std::string& error);

    //! Renders one page to PNG at `dpi`.
    bool renderPage(int pageIndex, int dpi, PageImage& out, std::string& error) const;

    //! How symbols are written into the SVG. Path is the default: it keeps a
    //! text selection in the page from dragging the notation along with it,
    //! and needs no music font at the far end.
    enum class SvgSymbols {
        //! <text> in the music font family: smaller, but the reader needs the
        //! font.
        Font,
        //! Glyph outlines in <defs>, referenced by <use>: self-contained.
        Path,
    };

    //! Whether the SVG carries the fonts it needs.
    enum class SvgFonts {
        //! Name them and let the reader resolve them.
        Reference,
        //! Embed a subset of each, cut to the code points the page uses.
        EmbedSubset,
    };

    //! Renders one page to an SVG document. `dpi` sets the coordinate scale;
    //! the result is vector either way.
    bool renderPageSvg(int pageIndex, int dpi, SvgSymbols symbols, SvgFonts fonts,
                       std::string& out, std::string& error) const;

    //! Every laid-out element, one line per element
    //! ("page type left top width" in tenths of a staff space), sorted.
    //!
    //! This is the regression check: it describes the layout without
    //! describing how it was drawn, so it stays identical across changes to
    //! the paint path, and moves when something perturbs layout -- which is
    //! exactly what swapping the Qt font provider for the FreeType one risks.
    //! Ported from aria's ScoreRenderSession::layoutFingerprint.
    bool layoutFingerprint(std::vector<std::string>& out, std::string& error) const;

private:
    class Impl;

    static std::unique_ptr<Session> openShared(std::shared_ptr<IReadStream> source,
                                              const ScoreInfo& info, std::string& error);

    explicit Session(std::unique_ptr<Impl> impl);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::unique_ptr<Impl> m_impl;
};

} // namespace mscz
