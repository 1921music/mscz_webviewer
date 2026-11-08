// SPDX-License-Identifier: GPL-3.0-only
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "tsf.h"
#include <emscripten/emscripten.h>
static tsf* bank;
static short pcm[512];
EMSCRIPTEN_KEEPALIVE int synth_font(const void* bytes, int length) {
    if (bank || !bytes || length <= 0) return 0;
    bank = tsf_load_memory(bytes, length);
    return bank != NULL;
}
EMSCRIPTEN_KEEPALIVE tsf* synth_create(void) {
    if (!bank) return NULL;
    tsf* s = tsf_copy(bank);
    if (!s) return NULL;
    tsf_set_output(s, TSF_STEREO_INTERLEAVED, 44100, -12);
    if (!tsf_set_max_voices(s, 64)) { tsf_close(s); return NULL; }
    for (int c = 0; c < 16; ++c) {
        if (!tsf_channel_set_presetnumber(s, c, 0, c == 9)) { tsf_close(s); return NULL; }
    }
    return s;
}
EMSCRIPTEN_KEEPALIVE int synth_event(tsf* s, int status, int a, int b) {
    if (!s || status < 0x80 || status >= 0xf0 || a < 0 || a > 127 || b < 0 || b > 127) return 0;
    int c = status & 15;
    switch (status & 0xf0) {
    case 0x80: tsf_channel_note_off(s, c, a); break;
    case 0x90: if (b) return tsf_channel_note_on(s, c, a, b / 127.f); tsf_channel_note_off(s, c, a); break;
    case 0xb0: return tsf_channel_midi_control(s, c, a, b);
    case 0xc0: return tsf_channel_set_presetnumber(s, c, a, c == 9);
    case 0xe0: return tsf_channel_set_pitchwheel(s, c, a | (b << 7));
    default: break;
    }
    return 1;
}
EMSCRIPTEN_KEEPALIVE const short* synth_render(tsf* s) {
    if (!s) return NULL;
    tsf_render_short(s, pcm, 256, 0); return pcm;
}
EMSCRIPTEN_KEEPALIVE void synth_destroy(tsf* s) { if (s) tsf_close(s); }
