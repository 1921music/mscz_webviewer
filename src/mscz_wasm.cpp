/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */

//! The browser's side of the facade: plain C entry points, no embind.
//!
//! embind would be friendlier, but it carries its own type registry and RTTI
//! into the binary, and size is this project's first constraint. These are a
//! dozen functions over integers and pointers, which `cwrap` can call directly
//! and which cost nothing. web/mscz.js wraps them into something pleasant.
//!
//! The shape of it:
//!
//!   1. JS fetches the resources the engine needs and hands each over by the
//!      ":/..." name the engine knows it by (mscz_resource_add).
//!   2. mscz_init starts the engine, with those resources as its only route to
//!      a file. There is no filesystem in this build, MEMFS included.
//!   3. mscz_open takes the .mscz bytes and gives back a session.
//!   4. mscz_render_page_svg leaves the SVG in a buffer JS then reads.
//!
//! Resources are pushed in rather than pulled, because a pull would have to be
//! synchronous -- the engine asks for a font in the middle of laying a page out
//! -- and the browser has no synchronous fetch on the main thread. Pushing
//! keeps the asynchrony in JS where it belongs. What has to be pushed before
//! the first render is small (the SMuFL metadata, the instrument definitions,
//! Leland and Edwin); the rest can be added later, since a provider added
//! before a render is visible to it.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>

#include "hostio.h"
#include "mscz_engine.h"
#include "textfontbackend.h"

EM_JS(int, has_text_backend, (), { return Module['textFontBackend'] ? 1 : 0; });
EM_JS(void, measure_browser_text, (const char* family, double px, int bold, int italic, const char* text, double* out), {
    const values = Module['textFontBackend'].measure(UTF8ToString(family), px, !!bold, !!italic, UTF8ToString(text));
    if (!values || values.length !== 10 || !values.every(Number.isFinite)) throw new Error('Invalid text font metrics');
    for (let i = 0; i < 10; ++i) HEAPF64[(out >> 3) + i] = values[i];
});
EM_JS(void, browser_text_family, (const char* family, char* out, int capacity), {
    stringToUTF8(Module['textFontBackend'].family(UTF8ToString(family)), out, capacity);
});

namespace {
class BrowserTextBackend final : public mscz::TextFontBackend {
public:
    bool handles(const muse::draw::Font&) const override { return true; }
    mscz::TextMetrics measure(const muse::draw::Font& font, const muse::String& text) const override {
        const double px = font.pointSizeF() > 0 ? font.pointSizeF() * 1200.0 / 72.0
                        : font.pixelSize() > 0 ? font.pixelSize() : 200.0;
        double m[10];
        measure_browser_text(font.family().id().toStdString().c_str(), px, font.bold(), font.italic(),
                             text.toStdString().c_str(), m);
        return {m[0], m[1], m[2], m[3], m[4], m[5], muse::RectF(m[6], m[7], m[8], m[9])};
    }
    muse::String svgFamily(const muse::draw::Font& font) const override {
        char family[2048];
        browser_text_family(font.family().id().toStdString().c_str(), family, sizeof(family));
        return muse::String::fromUtf8(family);
    }
};

//! Bytes JS handed over, served without another copy.
//!
//! The map owns them and its nodes are stable, so a stream can point into an
//! entry and stay valid. That matters: a CJK font is megabytes, and copying it
//! per open would be the largest allocation in the run.
class PushedResources : public mscz::IResourceProvider
{
public:
    void add(const std::string& name, std::vector<uint8_t> bytes)
    {
        m_entries[name] = std::move(bytes);
    }

    bool exists(const std::string& path) const override
    {
        return m_entries.find(path) != m_entries.end();
    }

    std::unique_ptr<mscz::IReadStream> open(const std::string& path) const override
    {
        auto it = m_entries.find(path);
        if (it == m_entries.end()) {
            return nullptr;
        }
        return std::unique_ptr<mscz::IReadStream>(new View(it->second));
    }

private:
    class View : public mscz::IReadStream
    {
    public:
        explicit View(const std::vector<uint8_t>& bytes)
            : m_bytes(bytes) {}

        uint64_t size() const override { return m_bytes.size(); }

        size_t read(uint8_t* dest, uint64_t pos, size_t len) override
        {
            if (pos >= m_bytes.size()) {
                return 0;
            }
            const size_t n = std::min<size_t>(len, m_bytes.size() - static_cast<size_t>(pos));
            std::memcpy(dest, m_bytes.data() + pos, n);
            return n;
        }

    private:
        const std::vector<uint8_t>& m_bytes;
    };

    std::map<std::string, std::vector<uint8_t> > m_entries;
};

std::shared_ptr<PushedResources> g_resources;
std::map<int, std::unique_ptr<mscz::Session> > g_sessions;
int g_nextSession = 1;

//! The last SVG, and the last error. JS reads them through
//! mscz_result_ptr/len, so they have to outlive the call that produced them.
std::string g_result;
std::string g_error;

PushedResources* resources()
{
    if (!g_resources) {
        g_resources = std::make_shared<PushedResources>();
    }
    return g_resources.get();
}

mscz::Session* session(int id)
{
    auto it = g_sessions.find(id);
    return it == g_sessions.end() ? nullptr : it->second.get();
}

} // namespace

extern "C" {

#ifdef MSCZ_ENABLE_PLAYBACK
EMSCRIPTEN_KEEPALIVE
int mscz_playback_timeline(int id)
{
    g_error.clear();
    auto* s = session(id);
    if (!s) { g_error = "no such session"; return 0; }
    return s->playbackTimeline(g_result, g_error) ? 1 : 0;
}
#endif

//! JS needs somewhere to put bytes before handing them over. Plain malloc, so
//! the usual HEAPU8.set() works.
EMSCRIPTEN_KEEPALIVE
void* mscz_alloc(size_t size)
{
    return std::malloc(size);
}

EMSCRIPTEN_KEEPALIVE
void mscz_free(void* ptr)
{
    std::free(ptr);
}

//! Hands a resource over by the name the engine asks for it by, for instance
//! ":/fonts/leland/Leland.otf". Copies, and frees nothing: the caller still
//! owns `data` and should mscz_free it.
EMSCRIPTEN_KEEPALIVE
void mscz_resource_add(const char* name, const uint8_t* data, size_t size)
{
    resources()->add(name, std::vector<uint8_t>(data, data + size));
}

EMSCRIPTEN_KEEPALIVE
int mscz_resource_exists(const char* name)
{
    return resources()->exists(name) ? 1 : 0;
}

//! Starts the engine. Resources pushed before this call are what it sees
//! first; anything pushed after is still visible, because the provider is the
//! same object.
EMSCRIPTEN_KEEPALIVE
void mscz_init()
{
    resources();
    mscz::setTextFontBackend(has_text_backend() ? std::make_shared<BrowserTextBackend>() : nullptr);
    mscz::Engine::setResourceProvider(g_resources);
    mscz::Engine::initializeIfNeeded();
}

//! Opens a score. Returns a session id, or 0 with mscz_last_error set.
//!
//! The bytes are copied into the session's own buffer, so the caller can free
//! `data` straight away. A .mscz is a zip the reader seeks around in, so it
//! has to stay available for as long as the score is open.
EMSCRIPTEN_KEEPALIVE
int mscz_open(const uint8_t* data, size_t size, const char* name)
{
    g_error.clear();

    mscz::ScoreInfo info;
    info.name = name && *name ? name : "score";

    auto stream = std::unique_ptr<mscz::IReadStream>(
        new mscz::BufferStream(std::vector<uint8_t>(data, data + size)));

    std::unique_ptr<mscz::Session> s = mscz::Session::open(std::move(stream), info, g_error);
    if (!s) {
        return 0;
    }

    const int id = g_nextSession++;
    g_sessions[id] = std::move(s);
    return id;
}

EMSCRIPTEN_KEEPALIVE
void mscz_close(int id)
{
    g_sessions.erase(id);
}

EMSCRIPTEN_KEEPALIVE
int mscz_page_count(int id)
{
    mscz::Session* s = session(id);
    return s ? s->pageCount() : 0;
}

EMSCRIPTEN_KEEPALIVE
int mscz_view_transpose_semitones(int id)
{
    mscz::Session* s = session(id);
    return s ? s->viewTransposeSemitones() : 0;
}

EMSCRIPTEN_KEEPALIVE
int mscz_set_view_transpose_semitones(int id, int semitones)
{
    g_error.clear();
    mscz::Session* s = session(id);
    if (!s) {
        g_error = "no such session";
        return 0;
    }
    return s->setViewTransposeSemitones(semitones, g_error) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int mscz_view_transpose_spelling(int id)
{
    auto s = session(id);
    return s ? int(s->viewTransposeSpelling()) : 0;
}

EMSCRIPTEN_KEEPALIVE
int mscz_set_view_transpose(int id, int semitones, int mode)
{
    g_error.clear();
    auto s = session(id);
    if (!s) { g_error = "no such session"; return 0; }
    if (mode != 0 && mode != 1) { g_error = "unknown spelling mode"; return 0; }
    return s->setViewTranspose(semitones, mscz::Session::TransposeSpelling(mode), g_error) ? 1 : 0;
}

//! Renders a page. `symbols` is 0 for <text> in the music font and 1 for
//! outlines in <defs>; `fonts` is 0 to name fonts and 1 to embed subsets of
//! them. 1/1 is the default the probe uses: self-contained, and a text
//! selection does not drag the notation along.
//!
//! Returns 1 on success, with the SVG at mscz_result_ptr for the next
//! mscz_result_len bytes, valid until the following call.
EMSCRIPTEN_KEEPALIVE
int mscz_render_page_svg(int id, int pageIndex, int dpi, int symbols, int fonts)
{
    g_error.clear();
    g_result.clear();

    mscz::Session* s = session(id);
    if (!s) {
        g_error = "no such session";
        return 0;
    }

    const auto symbolMode = symbols ? mscz::Session::SvgSymbols::Path : mscz::Session::SvgSymbols::Font;
    const auto fontMode = fonts ? mscz::Session::SvgFonts::EmbedSubset : mscz::Session::SvgFonts::Reference;

    if (!s->renderPageSvg(pageIndex, dpi, symbolMode, fontMode, g_result, g_error)) {
        g_result.clear();
        return 0;
    }
    return 1;
}

//! The layout fingerprint, one element per line. Same regression check the
//! native probe prints, so a browser build can be held to the same baseline.
EMSCRIPTEN_KEEPALIVE
int mscz_layout_fingerprint(int id)
{
    g_error.clear();
    g_result.clear();

    mscz::Session* s = session(id);
    if (!s) {
        g_error = "no such session";
        return 0;
    }

    std::vector<std::string> lines;
    if (!s->layoutFingerprint(lines, g_error)) {
        return 0;
    }

    for (const std::string& line : lines) {
        g_result += line;
        g_result += '\n';
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE
const char* mscz_result_ptr()
{
    return g_result.data();
}

EMSCRIPTEN_KEEPALIVE
int mscz_rostrum_info(int id)
{
    g_error.clear(); g_result.clear();
    auto* s = session(id);
    return s && s->rostrumInfo(g_result, g_error);
}

EMSCRIPTEN_KEEPALIVE
int mscz_rostrum_save(int id, const char* json)
{
    g_error.clear(); g_result.clear();
    auto* s = session(id);
    return s && s->saveRostrum(json ? json : "{}", g_result, g_error);
}

EMSCRIPTEN_KEEPALIVE
size_t mscz_result_len()
{
    return g_result.size();
}

EMSCRIPTEN_KEEPALIVE
const char* mscz_last_error()
{
    return g_error.c_str();
}

} // extern "C"
