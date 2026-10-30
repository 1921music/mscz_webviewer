/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

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
    static void initializeIfNeeded();
};

class Session
{
public:
    static std::unique_ptr<Session> open(const std::string& scorePath, std::string& error);

    ~Session();

    int pageCount() const;

    //! Renders one page to PNG at `dpi`.
    bool renderPage(int pageIndex, int dpi, PageImage& out, std::string& error) const;

    //! Renders one page to an SVG document. `dpi` only sets the SVG's
    //! intrinsic width and height; the coordinates themselves are resolution
    //! independent.
    bool renderPageSvg(int pageIndex, int dpi, std::string& out, std::string& error) const;

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

    explicit Session(std::unique_ptr<Impl> impl);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::unique_ptr<Impl> m_impl;
};

} // namespace mscz
