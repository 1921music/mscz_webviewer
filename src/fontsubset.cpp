/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "fontsubset.h"

#include <map>

#include <hb.h>
#include <hb-subset.h>

#include "global/io/file.h"
#include "global/log.h"
#include "global/modularity/ioc.h"

#include "draw/internal/ifontsdatabase.h"

using namespace muse;
using namespace muse::draw;

namespace mscz {

struct FontSubset::Impl {
    //! Font files read once and kept, since a page asks for the same handful
    //! of faces repeatedly.
    std::map<std::string, ByteArray> files;

    muse::GlobalInject<IFontsDatabase> fontsDatabase;

    const ByteArray* file(const io::path_t& path)
    {
        const std::string key = path.toStdString();
        auto it = files.find(key);
        if (it != files.end()) {
            return it->second.empty() ? nullptr : &it->second;
        }

        ByteArray& data = files[key];
        io::File f(path);
        if (!f.open(io::IODevice::ReadOnly)) {
            LOGE() << "could not open font " << path;
            return nullptr;
        }
        data = f.readAll();
        f.close();
        return data.empty() ? nullptr : &data;
    }
};

FontSubset::FontSubset()
    : m_impl(new Impl())
{
}

FontSubset::~FontSubset()
{
    delete m_impl;
}

FontSubset::FaceKey FontSubset::keyFor(const Font& font)
{
    FaceKey key;
    key.family = font.family().id().toStdString();
    key.bold = font.bold();
    key.italic = font.italic();
    key.type = font.type();
    return key;
}

std::vector<std::uint8_t> FontSubset::subset(const FaceKey& key, const std::set<char32_t>& codepoints)
{
    std::vector<std::uint8_t> out;
    if (codepoints.empty()) {
        return out;
    }

    const std::shared_ptr<IFontsDatabase> fdb = m_impl->fontsDatabase();
    if (!fdb) {
        return out;
    }

    const FontDataKey dataKey(Font::FontFamily(String::fromStdString(key.family)), key.bold, key.italic);
    const io::path_t path = fdb->fontPath(dataKey, key.type);
    if (path.empty()) {
        // A family the score named but we do not ship, Times New Roman for
        // instance. Nothing to embed; the reader falls back.
        return out;
    }

    const ByteArray* data = m_impl->file(path);
    if (!data) {
        return out;
    }

    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(data->constData()),
                                     static_cast<unsigned>(data->size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);

    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
        hb_face_destroy(face);
        return out;
    }

    hb_set_t* unicodes = hb_subset_input_unicode_set(input);
    for (const char32_t cp : codepoints) {
        hb_set_add(unicodes, cp);
    }

    // Keep the layout tables: dropping them would change text advances, and
    // the engine laid the page out expecting the kerning and ligatures the
    // full font has.
    hb_subset_input_set_flags(input,
                              static_cast<hb_subset_flags_t>(HB_SUBSET_FLAGS_RETAIN_GIDS
                                                             | HB_SUBSET_FLAGS_NAME_LEGACY));

    hb_face_t* subsetFace = hb_subset_or_fail(face, input);
    hb_subset_input_destroy(input);
    hb_face_destroy(face);

    if (!subsetFace) {
        LOGE() << "hb_subset failed for " << path;
        return out;
    }

    hb_blob_t* result = hb_face_reference_blob(subsetFace);
    unsigned length = 0;
    const char* bytes = hb_blob_get_data(result, &length);
    if (bytes && length > 0) {
        out.assign(reinterpret_cast<const std::uint8_t*>(bytes),
                   reinterpret_cast<const std::uint8_t*>(bytes) + length);
    }
    hb_blob_destroy(result);
    hb_face_destroy(subsetFace);

    return out;
}

std::string FontSubset::formatHint(const std::vector<std::uint8_t>& font)
{
    if (font.size() < 4) {
        return "opentype";
    }
    // 'OTTO' is CFF outlines; 0x00010000 and 'true' are TrueType.
    if (font[0] == 'O' && font[1] == 'T' && font[2] == 'T' && font[3] == 'O') {
        return "opentype";
    }
    return "truetype";
}

std::string FontSubset::base64(const std::vector<std::uint8_t>& bytes)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);

    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += table[(v >> 18) & 0x3f];
        out += table[(v >> 12) & 0x3f];
        out += table[(v >> 6) & 0x3f];
        out += table[v & 0x3f];
    }

    if (i < bytes.size()) {
        const bool two = (i + 1 < bytes.size());
        const std::uint32_t v = (bytes[i] << 16) | (two ? (bytes[i + 1] << 8) : 0);
        out += table[(v >> 18) & 0x3f];
        out += table[(v >> 12) & 0x3f];
        out += two ? table[(v >> 6) & 0x3f] : '=';
        out += '=';
    }

    return out;
}

} // namespace mscz
