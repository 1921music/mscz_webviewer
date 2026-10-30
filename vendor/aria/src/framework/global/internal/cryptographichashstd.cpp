/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * Added for mscz_test. See the header for why this is not a real MD4 or SHA.
 */
#include "cryptographichashstd.h"

#include <cstdint>

using namespace muse;

ByteArray CryptographicHashStd::hash(const ByteArray& data, Algorithm alg) const
{
    // Two FNV-1a passes with different seeds, so the result is 16 bytes and
    // the algorithm argument still changes the digest, as a caller comparing
    // hashes from different algorithms would expect.
    const std::uint64_t seed = 0xcbf29ce484222325ULL + static_cast<std::uint64_t>(alg);
    std::uint64_t a = seed;
    std::uint64_t b = seed ^ 0x9e3779b97f4a7c15ULL;

    const std::uint8_t* p = data.constData();
    const size_t n = data.size();
    for (size_t i = 0; i < n; ++i) {
        a = (a ^ p[i]) * 0x100000001b3ULL;
        b = (b ^ p[n - 1 - i]) * 0x100000001b3ULL;
    }

    ByteArray out(16);
    std::uint8_t* o = out.data();
    for (int i = 0; i < 8; ++i) {
        o[i] = static_cast<std::uint8_t>(a >> (8 * i));
        o[8 + i] = static_cast<std::uint8_t>(b >> (8 * i));
    }
    return out;
}
