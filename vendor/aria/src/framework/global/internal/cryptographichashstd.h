/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * Added for mscz_test. The stock CryptographicHash wraps QCryptographicHash,
 * so the Qt-free build needs its own.
 */
#ifndef MUSE_GLOBAL_CRYPTOGRAPHICHASHSTD_H
#define MUSE_GLOBAL_CRYPTOGRAPHICHASHSTD_H

#include "icryptographichash.h"

namespace muse {
//! A Qt-free ICryptographicHash.
//!
//! The only caller in this build is ImageStore::add, which hashes an embedded
//! image to find one it already holds -- a content key, not a security
//! measure. So this is FNV-1a, which is small, has no dependencies and is
//! stable, rather than a real MD4 or SHA.
//!
//! It therefore does NOT produce the digests the requested algorithms name. If
//! a hash ever has to match something outside this process -- a value written
//! into a saved file, say -- this has to be replaced with the real thing. We
//! only read scores, so nothing does today.
class CryptographicHashStd : public ICryptographicHash
{
public:
    ByteArray hash(const ByteArray& data, Algorithm alg) const override;
};
}

#endif // MUSE_GLOBAL_CRYPTOGRAPHICHASHSTD_H
