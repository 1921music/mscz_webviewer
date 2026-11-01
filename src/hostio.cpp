/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#include "hostio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "global/io/fileinfo.h"
#include "global/log.h"

using namespace muse;

namespace {

muse::Ret notSupported()
{
    return muse::make_ret(muse::Ret::Code::NotSupported);
}

//! Reads from a file on disk, one range at a time. The only part of this
//! project that opens a file, and the part a browser build swaps out.
class FileStream : public mscz::IReadStream
{
public:
    static std::unique_ptr<FileStream> open(const std::string& path)
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            return nullptr;
        }
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        if (size < 0) {
            std::fclose(f);
            return nullptr;
        }
        return std::unique_ptr<FileStream>(new FileStream(f, static_cast<uint64_t>(size)));
    }

    ~FileStream() override
    {
        if (m_file) {
            std::fclose(m_file);
        }
    }

    uint64_t size() const override { return m_size; }

    size_t read(uint8_t* dest, uint64_t pos, size_t len) override
    {
        if (!m_file || pos >= m_size) {
            return 0;
        }
        if (len > m_size - pos) {
            len = static_cast<size_t>(m_size - pos);
        }
        if (std::fseek(m_file, static_cast<long>(pos), SEEK_SET) != 0) {
            return 0;
        }
        return std::fread(dest, 1, len, m_file);
    }

private:
    FileStream(std::FILE* f, uint64_t size)
        : m_file(f), m_size(size) {}

    std::FILE* m_file = nullptr;
    uint64_t m_size = 0;
};

class DirectoryProvider : public mscz::IResourceProvider
{
public:
    DirectoryProvider(std::string root, const std::string& manifestPath)
        : m_root(std::move(root))
    {
        if (!manifestPath.empty()) {
            loadManifest(manifestPath);
        }
    }

    bool exists(const std::string& path) const override
    {
        std::FILE* f = std::fopen(resolve(path).c_str(), "rb");
        if (!f) {
            return false;
        }
        std::fclose(f);
        return true;
    }

    std::unique_ptr<mscz::IReadStream> open(const std::string& path) const override
    {
        return FileStream::open(resolve(path));
    }

private:
    //! A ":/..." name goes through the manifest when there is one, and
    //! otherwise just loses its prefix. Anything else is taken as given, so a
    //! score can be opened by its own path.
    std::string resolve(const std::string& path) const
    {
        if (path.rfind(":/", 0) != 0) {
            return path;
        }

        const std::string alias = path.substr(2);
        auto it = m_aliases.find(alias);
        if (it != m_aliases.end()) {
            return m_root + "/" + it->second;
        }
        return m_root + "/" + alias;
    }

    void loadManifest(const std::string& manifestPath)
    {
        std::FILE* f = std::fopen(manifestPath.c_str(), "rb");
        if (!f) {
            LOGW() << "no resource manifest at " << manifestPath
                   << "; \":/\" names will be taken literally";
            return;
        }

        std::string line;
        int c = 0;
        while ((c = std::fgetc(f)) != EOF) {
            if (c == '\n') {
                addManifestLine(line);
                line.clear();
            } else if (c != '\r') {
                line += static_cast<char>(c);
            }
        }
        addManifestLine(line);
        std::fclose(f);
    }

    void addManifestLine(const std::string& line)
    {
        if (line.empty() || line[0] == '#') {
            return;
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            return;
        }
        m_aliases.emplace(line.substr(0, tab), line.substr(tab + 1));
    }

    std::string m_root;
    std::map<std::string, std::string> m_aliases;
};

//! Serves resources compiled into the binary.
class EmbeddedProvider : public mscz::IResourceProvider
{
public:
    explicit EmbeddedProvider(const mscz::EmbeddedResource* table)
    {
        for (const mscz::EmbeddedResource* e = table; e && e->name; ++e) {
            m_entries.emplace(e->name, e);
        }
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
        return std::unique_ptr<mscz::IReadStream>(
            new mscz::BufferStream(std::vector<uint8_t>(it->second->data, it->second->data + it->second->size)));
    }

private:
    std::map<std::string, const mscz::EmbeddedResource*> m_entries;
};

//! Remembers what it has read, up to a budget.
class CachingProvider : public mscz::IResourceProvider
{
public:
    CachingProvider(std::shared_ptr<mscz::IResourceProvider> inner, size_t maxEntry, size_t maxTotal)
        : m_inner(std::move(inner)), m_maxEntry(maxEntry), m_maxTotal(maxTotal) {}

    bool exists(const std::string& path) const override
    {
        return m_cache.find(path) != m_cache.end() || (m_inner && m_inner->exists(path));
    }

    std::unique_ptr<mscz::IReadStream> open(const std::string& path) const override
    {
        auto it = m_cache.find(path);
        if (it != m_cache.end()) {
            return std::unique_ptr<mscz::IReadStream>(new View(it->second));
        }

        if (!m_inner) {
            return nullptr;
        }

        std::unique_ptr<mscz::IReadStream> stream = m_inner->open(path);
        if (!stream) {
            return nullptr;
        }

        const uint64_t size = stream->size();
        if (size > m_maxEntry || m_total + size > m_maxTotal) {
            // Too big to hold, or the budget is spent. Hand back the stream as
            // it is, so a large score keeps streaming.
            return stream;
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        size_t got = 0;
        while (got < bytes.size()) {
            const size_t n = stream->read(bytes.data() + got, got, bytes.size() - got);
            if (n == 0) {
                break;
            }
            got += n;
        }
        if (got != bytes.size()) {
            // A short read is not something to cache a wrong answer for.
            return m_inner->open(path);
        }

        m_total += bytes.size();
        auto added = m_cache.emplace(path, std::move(bytes)).first;
        return std::unique_ptr<mscz::IReadStream>(new View(added->second));
    }

private:
    //! Reads out of a cache entry without copying it. The map owns the bytes
    //! and its nodes are stable, so the reference stays good.
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

    std::shared_ptr<mscz::IResourceProvider> m_inner;
    size_t m_maxEntry = 0;
    size_t m_maxTotal = 0;

    // open() is const because the interface is read-only; the cache it fills
    // is an implementation detail of answering the same question faster.
    mutable std::map<std::string, std::vector<uint8_t> > m_cache;
    mutable size_t m_total = 0;
};

//! First provider that has it, wins.
class LayeredProvider : public mscz::IResourceProvider
{
public:
    explicit LayeredProvider(std::vector<std::shared_ptr<mscz::IResourceProvider> > providers)
        : m_providers(std::move(providers)) {}

    bool exists(const std::string& path) const override
    {
        for (const auto& p : m_providers) {
            if (p && p->exists(path)) {
                return true;
            }
        }
        return false;
    }

    std::unique_ptr<mscz::IReadStream> open(const std::string& path) const override
    {
        for (const auto& p : m_providers) {
            if (!p) {
                continue;
            }
            if (std::unique_ptr<mscz::IReadStream> s = p->open(path)) {
                return s;
            }
        }
        return nullptr;
    }

private:
    std::vector<std::shared_ptr<mscz::IResourceProvider> > m_providers;
};

} // namespace

namespace mscz {

// --- BufferStream ------------------------------------------------------------

BufferStream::BufferStream(std::vector<uint8_t> data)
    : m_data(std::move(data))
{
}

uint64_t BufferStream::size() const
{
    return m_data.size();
}

size_t BufferStream::read(uint8_t* dest, uint64_t pos, size_t len)
{
    if (pos >= m_data.size()) {
        return 0;
    }
    if (len > m_data.size() - pos) {
        len = static_cast<size_t>(m_data.size() - pos);
    }
    std::memcpy(dest, m_data.data() + pos, len);
    return len;
}

// --- StreamDevice ------------------------------------------------------------

StreamDevice::StreamDevice(std::unique_ptr<IReadStream> stream)
    : m_stream(std::move(stream))
{
}

StreamDevice::~StreamDevice() = default;

bool StreamDevice::doOpen(OpenMode m)
{
    // Read-only by construction.
    return m == OpenMode::ReadOnly && m_stream != nullptr;
}

size_t StreamDevice::dataSize() const
{
    return m_stream ? static_cast<size_t>(m_stream->size()) : 0;
}

size_t StreamDevice::readData(uint8_t* dest, size_t pos, size_t len) const
{
    return m_stream ? m_stream->read(dest, pos, len) : 0;
}

const uint8_t* StreamDevice::rawData() const
{
    // Deliberately null: there is no resident buffer, which is the point.
    return nullptr;
}

bool StreamDevice::resizeData(size_t)
{
    return false;
}

size_t StreamDevice::writeData(const uint8_t*, size_t)
{
    return 0;
}

// --- HostFileSystem ----------------------------------------------------------

HostFileSystem::HostFileSystem(std::shared_ptr<IResourceProvider> provider)
    : m_provider(std::move(provider))
{
}

muse::Ret HostFileSystem::exists(const muse::io::path_t& path) const
{
    if (!m_provider) {
        return muse::make_ret(muse::Ret::Code::InternalError);
    }
    return m_provider->exists(path.toStdString())
           ? muse::make_ok()
           : muse::make_ret(muse::Ret::Code::UnknownError);
}

muse::Ret HostFileSystem::readFile(const muse::io::path_t& filePath, muse::ByteArray& data) const
{
    if (!m_provider) {
        return muse::make_ret(muse::Ret::Code::InternalError);
    }

    std::unique_ptr<IReadStream> stream = m_provider->open(filePath.toStdString());
    if (!stream) {
        LOGE() << "not found: " << filePath;
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    const size_t size = static_cast<size_t>(stream->size());
    data.resize(size);
    if (size == 0) {
        return muse::make_ok();
    }

    // IFileSystem has no read-side stream, so a resource fetched this way is
    // read whole. Fine for what comes through here -- a font or an instrument
    // definition is parsed entirely anyway. Scores take the StreamDevice path.
    const size_t got = stream->read(data.data(), 0, size);
    if (got != size) {
        data.resize(got);
    }
    return muse::make_ok();
}

muse::RetVal<muse::ByteArray> HostFileSystem::readFile(const muse::io::path_t& filePath) const
{
    muse::RetVal<muse::ByteArray> rv;
    rv.ret = readFile(filePath, rv.val);
    return rv;
}

muse::RetVal<uint64_t> HostFileSystem::fileSize(const muse::io::path_t& path) const
{
    muse::RetVal<uint64_t> rv;
    std::unique_ptr<IReadStream> stream = m_provider ? m_provider->open(path.toStdString()) : nullptr;
    if (!stream) {
        rv.ret = muse::make_ret(muse::Ret::Code::UnknownError);
        return rv;
    }
    rv.ret = muse::make_ok();
    rv.val = stream->size();
    return rv;
}

muse::io::EntryType HostFileSystem::entryType(const muse::io::path_t& path) const
{
    return exists(path) ? muse::io::EntryType::File : muse::io::EntryType::Undefined;
}

// Everything below would change something, or needs a directory the provider
// does not model. Read-only by design.

muse::Ret HostFileSystem::remove(const muse::io::path_t&, bool) { return notSupported(); }
muse::Ret HostFileSystem::clear(const muse::io::path_t&) { return notSupported(); }
muse::Ret HostFileSystem::copy(const muse::io::path_t&, const muse::io::path_t&, bool) { return notSupported(); }
muse::Ret HostFileSystem::move(const muse::io::path_t&, const muse::io::path_t&, bool) { return notSupported(); }
muse::Ret HostFileSystem::makePath(const muse::io::path_t&) const { return notSupported(); }
muse::Ret HostFileSystem::makeLink(const muse::io::path_t&, const muse::io::path_t&) const { return notSupported(); }
muse::Ret HostFileSystem::writeFile(const muse::io::path_t&, const muse::ByteArray&) { return notSupported(); }

muse::RetVal<muse::io::paths_t> HostFileSystem::scanFiles(const muse::io::path_t&, const std::vector<std::string>&,
                                                          muse::io::ScanMode) const
{
    muse::RetVal<muse::io::paths_t> rv;
    rv.ret = notSupported();
    return rv;
}

void HostFileSystem::setAttribute(const muse::io::path_t&, muse::io::IFileSystem::Attribute) const {}
bool HostFileSystem::setPermissionsAllowedForAll(const muse::io::path_t&) const { return false; }

muse::RetVal<muse::io::StreamId> HostFileSystem::openStream(const muse::io::path_t&, muse::io::OpenMode)
{
    // IFileSystem's stream trio is write-only; reads go through readFile or,
    // for a score, StreamDevice.
    muse::RetVal<muse::io::StreamId> rv;
    rv.ret = notSupported();
    return rv;
}

muse::Ret HostFileSystem::writeToStream(muse::io::StreamId, const muse::ByteArray&, uint64_t)
{
    return notSupported();
}

muse::Ret HostFileSystem::closeStream(muse::io::StreamId)
{
    return notSupported();
}

muse::io::path_t HostFileSystem::canonicalFilePath(const muse::io::path_t& filePath) const { return filePath; }
muse::io::path_t HostFileSystem::absolutePath(const muse::io::path_t& filePath) const { return muse::io::dirpath(filePath); }
muse::io::path_t HostFileSystem::absoluteFilePath(const muse::io::path_t& filePath) const { return filePath; }
muse::DateTime HostFileSystem::birthTime(const muse::io::path_t&) const { return muse::DateTime(); }
muse::DateTime HostFileSystem::lastModified(const muse::io::path_t&) const { return muse::DateTime(); }
muse::Ret HostFileSystem::isWritable(const muse::io::path_t&) const { return notSupported(); }

// --- provider ----------------------------------------------------------------

std::shared_ptr<IResourceProvider> makeDirectoryProvider(const std::string& resourceRoot,
                                                         const std::string& manifestPath)
{
    return std::make_shared<DirectoryProvider>(resourceRoot, manifestPath);
}

std::shared_ptr<IResourceProvider> makeCachingProvider(std::shared_ptr<IResourceProvider> inner,
                                                       size_t maxEntryBytes, size_t maxTotalBytes)
{
    return std::make_shared<CachingProvider>(std::move(inner), maxEntryBytes, maxTotalBytes);
}

std::shared_ptr<IResourceProvider> makeEmbeddedProvider(const EmbeddedResource* table)
{
    return std::make_shared<EmbeddedProvider>(table);
}

std::shared_ptr<IResourceProvider> makeLayeredProvider(std::vector<std::shared_ptr<IResourceProvider> > providers)
{
    return std::make_shared<LayeredProvider>(std::move(providers));
}

} // namespace mscz

namespace mscz {

//! Answers the engine's questions about a score that came from a stream.
//! Dates come back unset, which is what IFileInfoProvider allows and what
//! nothing in the read path depends on.
class StreamFileInfoProvider : public mu::engraving::IFileInfoProvider
{
public:
    explicit StreamFileInfoProvider(ScoreInfo info)
        : m_info(std::move(info)) {}

    bool saved() const override { return false; }

    muse::io::path_t path() const override { return muse::io::path_t(m_info.name); }
    muse::io::path_t fileName(bool includingExtension = true) const override
    {
        const muse::io::path_t p(m_info.name);
        return includingExtension ? p : muse::io::completeBasename(p);
    }

    muse::io::path_t absoluteDirPath() const override { return muse::io::path_t(); }

    muse::String displayName() const override
    {
        return muse::io::completeBasename(muse::io::path_t(m_info.name)).toString();
    }

    muse::DateTime birthTime() const override { return muse::DateTime(); }
    muse::DateTime lastModified() const override { return muse::DateTime(); }
    bool isNewlyCreated() const override { return false; }

private:
    ScoreInfo m_info;
};

std::shared_ptr<mu::engraving::IFileInfoProvider> makeStreamFileInfoProvider(const ScoreInfo& info)
{
    return std::make_shared<StreamFileInfoProvider>(info);
}

} // namespace mscz
