/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.
 */

//! Renders every page of a score to PNG, to check the engine end to end from
//! the command line.
//!
//!   mscz_probe [--dpi N] [--out DIR] <score.mscz>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mscz_engine.h"

namespace {

int usage()
{
    std::fprintf(stderr,
                 "usage: mscz_probe [--dpi N] [--out DIR] <score.mscz|.mscx>\n"
                 "       mscz_probe --fingerprint <score.mscz|.mscx>\n");
    return 2;
}

bool writeFile(const std::string& path, const std::vector<unsigned char>& bytes)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return written == bytes.size();
}

} // namespace

int main(int argc, char* argv[])
{
    int dpi = 144;
    std::string outDir = ".";
    std::string scorePath;
    bool fingerprint = false;
    bool svg = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--dpi") == 0 && i + 1 < argc) {
            dpi = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--out") == 0 && i + 1 < argc) {
            outDir = argv[++i];
        } else if (std::strcmp(a, "--fingerprint") == 0) {
            fingerprint = true;
        } else if (std::strcmp(a, "--svg") == 0) {
            svg = true;
        } else if (a[0] == '-') {
            return usage();
        } else if (scorePath.empty()) {
            scorePath = a;
        } else {
            return usage();
        }
    }

    if (scorePath.empty() || dpi <= 0) {
        return usage();
    }

    std::string error;
    std::unique_ptr<mscz::Session> session = mscz::Session::open(scorePath, error);
    if (!session) {
        std::fprintf(stderr, "mscz_probe: %s\n", error.c_str());
        return 1;
    }

    if (fingerprint) {
        std::vector<std::string> lines;
        if (!session->layoutFingerprint(lines, error)) {
            std::fprintf(stderr, "mscz_probe: %s\n", error.c_str());
            return 1;
        }
        for (const std::string& line : lines) {
            std::printf("%s\n", line.c_str());
        }
        return 0;
    }

    const int pages = session->pageCount();
    std::printf("mscz_probe: %d page(s)\n", pages);

    for (int page = 0; page < pages; ++page) {
        char name[64];

        if (svg) {
            std::string doc;
            if (!session->renderPageSvg(page, dpi, doc, error)) {
                std::fprintf(stderr, "mscz_probe: page %d: %s\n", page + 1, error.c_str());
                return 1;
            }
            std::snprintf(name, sizeof(name), "/page-%03d.svg", page + 1);
            const std::string path = outDir + name;
            const std::vector<unsigned char> bytes(doc.begin(), doc.end());
            if (!writeFile(path, bytes)) {
                std::fprintf(stderr, "mscz_probe: could not write %s\n", path.c_str());
                return 1;
            }
            std::printf("  %s  %zu bytes\n", path.c_str(), doc.size());
            continue;
        }

        mscz::PageImage image;
        if (!session->renderPage(page, dpi, image, error)) {
            std::fprintf(stderr, "mscz_probe: page %d: %s\n", page + 1, error.c_str());
            return 1;
        }

        std::snprintf(name, sizeof(name), "/page-%03d.png", page + 1);
        const std::string path = outDir + name;
        if (!writeFile(path, image.data)) {
            std::fprintf(stderr, "mscz_probe: could not write %s\n", path.c_str());
            return 1;
        }

        std::printf("  %s  %dx%d  %zu bytes\n", path.c_str(), image.pixelWidth, image.pixelHeight, image.data.size());
    }

    return 0;
}
