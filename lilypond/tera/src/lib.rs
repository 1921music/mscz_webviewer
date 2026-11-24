// SPDX-License-Identifier: GPL-3.0-only
mod openbook;
#[path = "../../src/wasm_bridge.rs"]
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
