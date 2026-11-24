// SPDX-License-Identifier: GPL-3.0-only
use crate::{Options, convert};

#[unsafe(no_mangle)]
pub extern "C" fn lilypond_convert(numbering: u32) -> *const u8 {
    super::wasm_bridge::render(|source| {
        convert(
            source,
            Options {
                measure_numbers: match numbering {
                    1 => Some("system"),
                    2 => Some("none"),
                    _ => None,
                },
                ..Options::default()
            },
        )
    })
}
