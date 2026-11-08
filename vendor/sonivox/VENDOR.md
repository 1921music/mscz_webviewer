# SONiVOX minimal GM synthesizer

Source: https://github.com/EmbeddedSynth/sonivox
Revision: 86bd07341078084cb9f224b9e7d508fbf4751af0 (4.0.2.0).
License: Apache-2.0; LICENSE and source notices retained.

Only the wavetable engine is built. Fixed 44.1 kHz, stereo, 64 voices per part,
8-bit embedded bank. SF2/DLS, FM, file parsers, reverb and chorus are disabled.
The application supplies in-memory MIDI events and consumes 256-frame PCM blocks.

Local changes: eas_config.c disables the mandatory SMF parser registration;
eas_public.c guards chorus/reverb parameter overrides when those effects are
compiled out. Build configuration and the wrapper live in src/synth and
cmake/MsczPlayback.cmake. The bank and its Apache notices are unchanged.
