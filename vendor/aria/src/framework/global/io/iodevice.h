/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <cstdint>
#include <string>
#include <map>

#include "global/types/bytearray.h"

namespace muse::io {
class IODevice
{
public:

    enum OpenMode {
        Unknown, ReadOnly, WriteOnly, ReadWrite, Append
    };

    IODevice() = default;
    virtual ~IODevice() = default;

    bool open(OpenMode m = ReadOnly);
    void close();

    bool isOpen() const;
    OpenMode openMode() const;
    bool isReadable() const;
    bool isWriteable() const;

    size_t size() const;
    size_t pos() const;

    bool seek(size_t pos);

    size_t read(uint8_t* data, size_t len);
    ByteArray read(size_t count);
    ByteArray readAll();

    const uint8_t* readData();

    size_t write(const uint8_t* data, size_t len);
    size_t write(const ByteArray& ba);

#ifndef NO_QT_SUPPORT
    size_t write(const QByteArray& ba);
#endif

    const std::string& meta(const std::string& key) const;
    void setMeta(const std::string& key, const std::string& val);

    bool hasError() const;
    int error() const;
    const std::string& errorString() const;

protected:

    virtual bool doOpen(OpenMode m) = 0;
    virtual void doClose() {}
    virtual size_t dataSize() const = 0;
    virtual bool resizeData(size_t size) = 0;
    virtual size_t writeData(const uint8_t* data, size_t len) = 0;

    //! mscz_test: rawData() is what forces a device to hold its whole content
    //! in memory, and it used to be the only way to read. A device that can
    //! seek and read a range instead -- a file on disk, a byte range over the
    //! network -- overrides readData() and returns nullptr from rawData().
    //!
    //! The default implementation keeps every existing device working
    //! unchanged: it copies out of the resident buffer.
    //!
    //! `pos` is absolute. Returns how much was actually copied.
    virtual size_t readData(uint8_t* dest, size_t pos, size_t len) const;

    //! The whole content, or nullptr for a device that does not hold it.
    //! Prefer readData(); this stays for devices that genuinely are a buffer.
    virtual const uint8_t* rawData() const = 0;

    bool isOpenModeReadable() const;
    bool isOpenModeWriteable() const;

    void setError(int error, const std::string& errorString);

private:


    OpenMode m_mode = OpenMode::Unknown;
    size_t m_pos = 0;
    std::map<std::string, std::string> m_meta;

    int m_error = 0;
    std::string m_errorString;
};
}
