/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "global/io/iodevice.h"
#include "global/io/ifilesystem.h"

#include "engraving/infrastructure/ifileinfoprovider.h"

//! File access lives out here, not in the engine.
//!
//! The host says how bytes are obtained -- off a disk, out of a buffer, over
//! the network -- and the engine only ever sees a seekable reader. That keeps
//! std::filesystem, or anything else platform-shaped, on this side of the
//! line, and it means a large score or a 16MB CJK font does not have to be
//! resident to be used: ZIP seeks to its central directory and then reads one
//! entry at a time, so the reads follow.
namespace mscz {

//! A seekable, read-only source of bytes.
class IReadStream
{
public:
    virtual ~IReadStream() = default;

    virtual uint64_t size() const = 0;

    //! Reads up to `len` bytes at absolute `pos`. Returns how many were read;
    //! short reads mean end of data, 0 means nothing left or an error.
    virtual size_t read(uint8_t* dest, uint64_t pos, size_t len) = 0;
};

//! Opens resources by the names the engine uses: a score's path, and the
//! ":/fonts/..." and ":/engraving/instruments/..." names that are Qt resource
//! paths in a Qt build and just names here.
class IResourceProvider
{
public:
    virtual ~IResourceProvider() = default;

    virtual bool exists(const std::string& path) const = 0;

    //! Null if there is nothing by that name.
    virtual std::unique_ptr<IReadStream> open(const std::string& path) const = 0;
};

//! Adapts an IReadStream to the engine's device, so MscReader can be handed
//! one directly. Overrides readData() and leaves rawData() null, which is what
//! keeps the content from being pulled in whole.
class StreamDevice : public muse::io::IODevice
{
public:
    explicit StreamDevice(std::unique_ptr<IReadStream> stream);
    ~StreamDevice() override;

protected:
    bool doOpen(OpenMode m) override;
    size_t dataSize() const override;
    size_t readData(uint8_t* dest, size_t pos, size_t len) const override;
    const uint8_t* rawData() const override;
    bool resizeData(size_t size) override;
    size_t writeData(const uint8_t* data, size_t len) override;

private:
    std::unique_ptr<IReadStream> m_stream;
};

//! The engine's IFileSystem, answered from an IResourceProvider.
//!
//! Read-only: everything that would change something returns not-supported.
//! IFileSystem's read side is readFile() -> whole ByteArray with no stream, so
//! resources fetched through here are read whole -- a font has to be parsed
//! entirely anyway. The streaming path is StreamDevice, handed straight to
//! MscReader.
class HostFileSystem : public muse::io::IFileSystem
{
public:
    explicit HostFileSystem(std::shared_ptr<IResourceProvider> provider);

    muse::Ret exists(const muse::io::path_t& path) const override;
    muse::Ret remove(const muse::io::path_t& path, bool onlyIfEmpty = false) override;
    muse::Ret clear(const muse::io::path_t& path) override;
    muse::Ret copy(const muse::io::path_t& src, const muse::io::path_t& dst, bool replace = false) override;
    muse::Ret move(const muse::io::path_t& src, const muse::io::path_t& dst, bool replace = false) override;

    muse::Ret makePath(const muse::io::path_t& path) const override;
    muse::Ret makeLink(const muse::io::path_t& targetPath, const muse::io::path_t& linkPath) const override;

    muse::io::EntryType entryType(const muse::io::path_t& path) const override;

    muse::RetVal<uint64_t> fileSize(const muse::io::path_t& path) const override;

    muse::RetVal<muse::io::paths_t> scanFiles(const muse::io::path_t& rootDir, const std::vector<std::string>& filters,
                                              muse::io::ScanMode mode
                                                  = muse::io::ScanMode::FilesInCurrentDirAndSubdirs) const override;

    void setAttribute(const muse::io::path_t& path, muse::io::IFileSystem::Attribute attribute) const override;
    bool setPermissionsAllowedForAll(const muse::io::path_t& path) const override;

    muse::RetVal<muse::ByteArray> readFile(const muse::io::path_t& filePath) const override;
    muse::Ret readFile(const muse::io::path_t& filePath, muse::ByteArray& data) const override;
    muse::Ret writeFile(const muse::io::path_t& filePath, const muse::ByteArray& data) override;

    muse::RetVal<muse::io::StreamId> openStream(const muse::io::path_t& filePath, muse::io::OpenMode mode) override;
    muse::Ret writeToStream(muse::io::StreamId fileId, const muse::ByteArray& data,
                            uint64_t offset = muse::io::STREAM_POS_CURRENT) override;
    muse::Ret closeStream(muse::io::StreamId fileId) override;

    muse::io::path_t canonicalFilePath(const muse::io::path_t& filePath) const override;
    muse::io::path_t absolutePath(const muse::io::path_t& filePath) const override;
    muse::io::path_t absoluteFilePath(const muse::io::path_t& filePath) const override;
    muse::DateTime birthTime(const muse::io::path_t& filePath) const override;
    muse::DateTime lastModified(const muse::io::path_t& filePath) const override;
    muse::Ret isWritable(const muse::io::path_t& filePath) const override;

private:
    std::shared_ptr<IResourceProvider> m_provider;
};

//! The engine wants to know a score's name and dates, which normally come
//! from the file it was read from. A score opened from a stream has no file,
//! so the caller supplies whatever it knows -- often just a display name.
//!
//! Not optional: MscLoader reads fileInfo()->fileName() while loading, so a
//! score without one crashes.
struct ScoreInfo {
    //! Shown to the user and used as the document name. A bare file name is
    //! the usual thing.
    std::string name = "score";
};

//! An IReadStream over a buffer the caller already holds.
class BufferStream : public IReadStream
{
public:
    explicit BufferStream(std::vector<uint8_t> data);

    uint64_t size() const override;
    size_t read(uint8_t* dest, uint64_t pos, size_t len) override;

private:
    std::vector<uint8_t> m_data;
};

//! An IResourceProvider reading from a directory tree. This is the host-side
//! piece: the only place that touches a real filesystem, and the one a browser
//! build replaces with something that fetches.
//!
//! `manifestPath` maps the ":/..." names the engine uses onto files under
//! `resourceRoot`. Those names are Qt resource aliases and do not match the
//! tree -- ":/engraving/instruments/instruments.xml" is actually
//! share/instruments/instruments.xml -- so the mapping is generated from the
//! .qrc files by scripts/gen-resource-manifest.py. Without a manifest, ":/" is
//! simply stripped, which is enough for a tree laid out to match.
std::shared_ptr<IResourceProvider> makeDirectoryProvider(const std::string& resourceRoot,
                                                         const std::string& manifestPath = std::string());

//! An IResourceProvider over resources compiled into the binary.
//!
//! `table` is a null-terminated array of entries; the last has a null name.
//! A build generates one of these from the .qrc list the way aria generates
//! its MPE profiles, and then those resources cost nothing to look up and need
//! no files at all.
struct EmbeddedResource {
    const char* name;           //!< the ":/..." name, prefix included
    const unsigned char* data;
    size_t size;
};

std::shared_ptr<IResourceProvider> makeEmbeddedProvider(const EmbeddedResource* table);

//! Keeps small resources in memory after the first read.
//!
//! Not a micro-optimisation. The engine re-reads a resource every time it
//! wants it -- FontsDatabase::fontData() goes back to the file on each call by
//! design, which is what makes its lazy loading lazy -- so a provider backed
//! by slow storage is asked for the same font over and over. Measured on this
//! machine with the resource tree on a Windows drive seen through WSL,
//! goldberg's layout took 978 seconds -- of which only 12.9 were CPU, so
//! nothing was computing, it was all waiting -- against 2.4 seconds with the
//! same tree on the Linux filesystem. Caching removes the difference, and
//! makes the native providers behave like the browser one, which holds its
//! bytes anyway.
//!
//! Bounded on purpose. Anything larger than `maxEntryBytes` is passed straight
//! through, so a large score still streams rather than becoming resident, and
//! caching stops once `maxTotalBytes` is reached.
std::shared_ptr<IResourceProvider> makeCachingProvider(std::shared_ptr<IResourceProvider> inner,
                                                       size_t maxEntryBytes = 4u * 1024 * 1024,
                                                       size_t maxTotalBytes = 32u * 1024 * 1024);

//! Tries each provider in turn and takes the first that has the resource.
//!
//! This is what makes the single seam pay off: whether a given resource is
//! baked into the binary or fetched when first needed is decided here, by
//! ordering, and can differ per resource. The engine knows none of it -- it
//! asks for a name and gets bytes. So the small things that are always needed
//! (styles, the SMuFL metadata, the instrument definitions) can be embedded
//! while a 16MB CJK font stays behind a fetch.
std::shared_ptr<IResourceProvider> makeLayeredProvider(std::vector<std::shared_ptr<IResourceProvider> > providers);

//! The score's identity, for a score that has no file behind it.
std::shared_ptr<mu::engraving::IFileInfoProvider> makeStreamFileInfoProvider(const ScoreInfo& info);

} // namespace mscz
