// SPDX-License-Identifier: GPL-3.0-only
use lilypond_musicxml::{Options, convert};

mod openbook;
mod wasm_bridge;

type Result<T> = std::result::Result<T, String>;

fn check(condition: bool, message: impl Into<String>) -> Result<()> {
    if condition {
        Ok(())
    } else {
        Err(message.into())
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn lilypond_render_template() -> *const u8 {
    wasm_bridge::render(openbook::render)
}

#[unsafe(no_mangle)]
pub extern "C" fn lilypond_convert(numbering: u32) -> *const u8 {
    wasm_bridge::render(|source| {
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
