# Optional GM SoundFont

sonivox.sf3 is a local Vorbis quality-3 conversion of alphaTab's sonivox.sf2:
https://github.com/CoderLine/alphaTab/tree/fe75d7c28015378434655c0476271018d635aa9e/packages/alphatab/font/sonivox

Original SF2 SHA-256: 4978cf37e7164206f2d22dc4fdf4020ffe040787210bf781e31bdc79961f59ef
SF3 SHA-256: 1997c2d32b042e81cb5e2c7070b6e8cc63681b0dd703009a9c847c62891c7a0d

The original LICENSE and editing history (UPSTREAM.md) are retained. The SF3
conversion encodes the 182 source samples individually with FFmpeg 8.1.2
libvorbis quality 3, preserving instrument/preset tables and relative loop points.
The reproducible converter is scripts/convert-gm-sf3.py (run through mise with
FFmpeg 8.1.2 available). No samples are added by this conversion.

Included only by MSCZ_PLAYBACK=TSF_SF3. EAS uses its own embedded bank and OFF
contains neither. The player offers GM only, with no runtime bank replacement UI.
