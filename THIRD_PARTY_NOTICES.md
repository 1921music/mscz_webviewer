# Third-party notices

## aria and MuseScore Studio

This project uses substantial portions of [aria](https://github.com/hdi200/aria),
which is derived from MuseScore Studio. The vendored engine's provenance and
local changes are recorded in [vendor/aria/VENDOR.md](vendor/aria/VENDOR.md).

Additional code, including the MusicXML importer, is taken directly from
[MuseScore Studio v4.7.5](https://github.com/musescore/MuseScore/tree/v4.7.5),
commit `3654226c2e99289916916953a98e585a3d3b315a`.
Its source hashes and local changes are recorded in
[vendor/aria/src/importexport/musicxml/SOURCE.json](vendor/aria/src/importexport/musicxml/SOURCE.json).

These components are licensed under GPL-3.0-only. Original copyright notices,
SPDX identifiers, and upstream notices in individual files remain applicable.
This project is a modified browser viewer; it is not an official aria or
MuseScore Studio release. The corresponding source and build instructions for
the published standalone viewer are available in this repository.

The root [LICENSE.txt](LICENSE.txt) is an unmodified copy of aria's LICENSE.txt
from the following revision:

[aria LICENSE.txt at 3e4d1c24029d18417636742bcbff6fded604ea3a](https://raw.githubusercontent.com/hdi200/aria/3e4d1c24029d18417636742bcbff6fded604ea3a/LICENSE.txt)

## Fonts and other dependencies

Third-party fonts and libraries retain their own licenses; the project's GPL
license does not replace them. Refer to their copyright notices and license
files, including:

- [Edwin — SIL OFL 1.1](vendor/aria/fonts/edwin/LICENSE.txt)
- [Leland — SIL OFL 1.1](vendor/aria/fonts/leland/LICENSE.txt)
- [Campania — SIL OFL 1.1](vendor/aria/fonts/campania/LICENSE)
- [FreeType — license choices and notices](vendor/thirdparty/freetype/LICENSE.TXT)
- [HarfBuzz — license and notices](vendor/thirdparty/harfbuzz/COPYING)
- [zlib — license and notices](vendor/thirdparty/zlib/LICENSE)

Other dependencies under `vendor/aria/thirdparty` and
`vendor/aria/src/framework/global/thirdparty` include their own license files
or per-file license notices. Preserve those notices when redistributing them.

## Optional playback

The EAS build includes SONiVOX (Sonic Network / Android Open Source Project),
Apache-2.0, with its embedded GM bank. See `vendor/sonivox/LICENSE` and
`vendor/sonivox/VENDOR.md` for the pinned revision and local portability changes.

The TSF_SF3 build includes TinySoundFont (MIT; Bernhard Schelling, based on
SFZero by Steve Folta), and stb_vorbis (Sean Barrett and contributors,
MIT/public-domain dual license). Notices are retained in `vendor/tinysoundfont`
and `vendor/stb/stb_vorbis.c`. The SF3 bank is a lossy conversion of alphaTab's
SONiVOX-derived SF2; its original license, editing history and conversion details
are retained in `assets/playback/`. It is not the same bank as EAS's embedded bank.

The audio licenses and provenance are retained in this repository. Selected
notices are also copied to `playback-LICENSE.txt` in the HTTP payload; they are
not displayed in the web application. No playback dependencies are shipped
by OFF builds. Existing GPL-3.0-only terms for the complete viewer remain in place.
