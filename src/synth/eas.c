// SPDX-License-Identifier: GPL-3.0-only
#include "eas.h"
#include <stdlib.h>
#include <emscripten/emscripten.h>
typedef struct { EAS_DATA_HANDLE engine; EAS_HANDLE stream; EAS_PCM pcm[512]; } Synth;
EMSCRIPTEN_KEEPALIVE Synth* synth_create(void) {
    Synth* s = calloc(1, sizeof(Synth));
    if (!s) return NULL;
    if (EAS_Init(&s->engine) != EAS_SUCCESS) { free(s); return NULL; }
    if (EAS_OpenMIDIStream(s->engine, &s->stream, NULL) != EAS_SUCCESS) { EAS_Shutdown(s->engine); free(s); return NULL; }
    EAS_SetVolume(s->engine, NULL, 88);
    return s;
}
EMSCRIPTEN_KEEPALIVE int synth_font(const void* bytes, int length) { return length == 0; }
EMSCRIPTEN_KEEPALIVE int synth_event(Synth* s, int status, int a, int b) {
    if (!s || status < 0x80 || status >= 0xf0 || a < 0 || a > 127 || b < 0 || b > 127) return 0;
    EAS_U8 bytes[3] = {status, a, b};
    int count = ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 2 : 3;
    return EAS_WriteMIDIStream(s->engine, s->stream, bytes, count) == EAS_SUCCESS;
}
EMSCRIPTEN_KEEPALIVE const short* synth_render(Synth* s) {
    if (!s) return NULL;
    EAS_I32 count = 0;
    if (EAS_Render(s->engine, s->pcm, 256, &count) != EAS_SUCCESS || count != 256) return NULL;
    return s->pcm;
}
EMSCRIPTEN_KEEPALIVE void synth_destroy(Synth* s) {
    if (!s) return;
    EAS_CloseMIDIStream(s->engine, s->stream); EAS_Shutdown(s->engine); free(s);
}
