// SPDX-License-Identifier: GPL-3.0-only
use crate::{Options, convert, convert_openbook};
use std::cell::RefCell;

thread_local! {
    // A conversion is one synchronous JS call. The buffers stay owned by
    // Rust and are reused across calls. JS copies the reply before the next.
    static INPUT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
    static OUTPUT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
}

#[unsafe(no_mangle)]
pub extern "C" fn lilypond_input(len: u32) -> *mut u8 {
    INPUT.with_borrow_mut(|input| {
        input.resize(len as usize, 0);
        input.as_mut_ptr()
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn lilypond_convert(openbook: u32, numbering: u32) -> *const u8 {
    let result = INPUT.with_borrow(|input| {
        let source = std::str::from_utf8(input).map_err(|_| "Expected a UTF-8 LilyPond file")?;
        // File::read_to_string in the native CLI accepts the same UTF-8 bytes.
        let options = Options {
            measure_numbers: match numbering {
                1 => Some("system"),
                2 => Some("none"),
                _ => None,
            },
            ..Options::default()
        };
        if openbook != 0 {
            convert_openbook(source, options)
        } else {
            convert(source, options)
        }
    });
    let (status, value) = match result {
        Ok(xml) => (0, xml),
        Err(error) => (1, error),
    };
    // Result header: total size (little endian), then a status byte.
    OUTPUT.with_borrow_mut(|output| {
        let len = 5 + value.len() as u32;
        output.clear();
        output.extend_from_slice(&len.to_le_bytes());
        output.push(status);
        output.extend_from_slice(value.as_bytes());
        output.as_ptr()
    })
}
