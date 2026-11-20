// SPDX-License-Identifier: GPL-3.0-only
//! Assemble the default lead-sheet version of an OpenBook song. The ordering
//! and labels follow OpenBook's common.ly.weave and scripts/drivers.py.
use crate::{Result, check};
use std::collections::BTreeMap;
use std::fmt::Write;

type Fields<'a> = BTreeMap<&'a str, &'a str>;

fn string_literal(source: &str) -> Result<String> {
    let quote = source.chars().next().ok_or("Missing quoted TOML value")?;
    check(matches!(quote, '"' | '\''), "Expected a quoted TOML value")?;
    let mut chars = source[quote.len_utf8()..].chars();
    let mut result = String::new();
    while let Some(ch) = chars.next() {
        if ch == quote {
            return Ok(result);
        }
        if ch != '\\' || quote == '\'' {
            result.push(ch);
            continue;
        }
        let escaped = match chars.next() {
            Some(ch @ ('"' | '\\')) => ch,
            Some('b') => '\x08',
            Some('t') => '\t',
            Some('n') => '\n',
            Some('f') => '\x0c',
            Some('r') => '\r',
            Some(ch @ ('u' | 'U')) => {
                let count = if ch == 'u' { 4 } else { 8 };
                let digits: String = chars.by_ref().take(count).collect();
                let code = u32::from_str_radix(&digits, 16).ok();
                code.filter(|_| digits.len() == count)
                    .and_then(char::from_u32)
                    .ok_or("Invalid TOML Unicode escape")?
            }
            _ => return Err("Unsupported TOML string escape".into()),
        };
        result.push(escaped);
    }
    Err("Unclosed OpenBook metadata string".into())
}

fn parts(mut body: &str) -> Result<Fields<'_>> {
    let mut sections = BTreeMap::new();
    while let Some((_, directive)) = body.split_once("{%") {
        let (directive, music) = directive.split_once("%}").ok_or("Unclosed Tera tag")?;
        let Some(condition) = directive.trim().strip_prefix("if") else {
            return Err(format!(
                "Unsupported OpenBook Tera tag: {}",
                directive.trim()
            ));
        };
        let label = condition
            .trim()
            .strip_prefix("part")
            .and_then(|s| s.trim().strip_prefix("=="))
            .and_then(|s| s.trim().strip_prefix('"'))
            .and_then(|s| s.strip_suffix('"'))
            .ok_or("Expected an OpenBook part condition")?;
        let (section, closing) = music.split_once("{%").ok_or("Missing Tera endif")?;
        let (closing, rest) = closing.split_once("%}").ok_or("Unclosed Tera endif")?;
        check(
            closing.trim() == "endif",
            "Nested Tera directives are unsupported",
        )?;
        check(
            !section.contains("{{"),
            "Tera expressions in song parts are unsupported",
        )?;
        check(
            sections.insert(label, section).is_none(),
            format!("Duplicate OpenBook part {label}"),
        )?;
        body = rest;
    }
    Ok(sections)
}

fn quoted(output: &mut String, text: &str) {
    output.push('"');
    // OpenBook's TOML values already contain LilyPond-level escapes.
    output.push_str(text);
    output.push('"');
}

fn row(output: &mut String, text: &str, title: bool) {
    output.push_str("\\markup \\fill-line { ");
    if title {
        output.push_str("\\larger ");
    }
    quoted(output, text);
    output.push_str(" }\n");
}

pub fn render(source: &str) -> Result<String> {
    let source = source.trim_start_matches('\u{feff}').trim_start();
    let (metadata, body) = source
        .strip_prefix("{#")
        .and_then(|s| s.split_once("#}"))
        .ok_or("Expected OpenBook .ly.tera metadata at the start of the file")?;
    let mut root = Fields::new();
    let mut attributes = Fields::new();
    let mut versions: BTreeMap<&str, Fields<'_>> = BTreeMap::new();
    let mut table = "";
    for line in metadata.lines().map(str::trim) {
        if let Some(name) = line.strip_prefix('[').and_then(|s| s.strip_suffix(']')) {
            table = name;
        } else if let Some((key, value)) = line.split_once('=')
            && !key.starts_with('#')
        {
            let field = (key.trim(), value.trim());
            match table {
                "" => {
                    root.insert(field.0, field.1);
                }
                "attributes" => {
                    attributes.insert(field.0, field.1);
                }
                _ => {
                    if let Some(name) = table.strip_prefix("versions.") {
                        versions.entry(name).or_default().insert(field.0, field.1);
                    }
                }
            }
        }
    }
    let version = string_literal(
        root.get("default_version")
            .ok_or("Missing default_version")?,
    )?;
    let flags = versions
        .get(version.as_str())
        .ok_or_else(|| format!("Missing OpenBook version {version}"))?;
    let enabled = |flag: &str| {
        flags
            .get(flag)
            .is_some_and(|value| value.split('#').next().unwrap().trim() == "true")
    };
    let get = |key: &str| attributes.get(key).map(|s| string_literal(s)).transpose();
    let title = get("title")?.ok_or("Missing OpenBook title")?;
    let composer = get("composer")?;
    let poet = get("poet")?;
    let heb = attributes.get("heb").is_some_and(|s| *s == "true");
    let sections = parts(body)?;
    let part = |name: &str| {
        sections
            .get(name)
            .copied()
            .ok_or_else(|| format!("Missing OpenBook part {name}"))
    };

    let mut ly = String::from("\\bookpart {\n");
    if let Some(dedication) = get("dedication")? {
        row(&mut ly, &dedication, false);
    }
    row(&mut ly, &title, true);
    if let Some(subtitle) = get("subtitle")? {
        row(&mut ly, &subtitle, true);
    }
    if let Some(subtitle) = get("subsubtitle")? {
        row(&mut ly, &subtitle, false);
    }
    if let (Some(composer), Some(poet)) = (&composer, &poet)
        && composer == poet
    {
        let label = if heb {
            "\u{5de}\u{5d9}\u{5dc}\u{5d9}\u{5dd} \u{5d5}\u{5dc}\u{5d7}\u{5df}: "
        } else {
            "Lyrics and Music by "
        };
        row(&mut ly, &format!("{label}{poet}"), false);
    } else {
        for (name, label) in [
            (
                poet,
                if heb {
                    "\u{5de}\u{5dc}\u{5d9}\u{5dd}: "
                } else {
                    "Lyrics by "
                },
            ),
            (
                composer,
                if heb {
                    "\u{5dc}\u{5d7}\u{5df}: "
                } else {
                    "Music by "
                },
            ),
        ] {
            if let Some(name) = name {
                row(&mut ly, &format!("{label}{name}"), false);
            }
        }
    }
    for key in ["piece", "opus"] {
        if let Some(value) = get(key)? {
            row(&mut ly, &value, false);
        }
    }

    if enabled("doOwn") {
        ly.push_str(part("Own")?);
    } else {
        if enabled("doPrep") {
            ly.push_str(part("Prep")?);
        }
        ly.push_str("\n\\score { <<\n");
        if enabled("doChords") {
            writeln!(
                ly,
                "\\new ChordNames {{ {} }}",
                part(&format!("Chords{version}"))?
            )
            .unwrap();
        }
        if enabled("doVoice") {
            writeln!(
                ly,
                "\\new Staff {{ \\new Voice = \"Voice\" {{ \\relative c' {} }} }}",
                part(&format!("Voice{version}"))?
            )
            .unwrap();
        }
        for (flag, name) in [
            ("doLyrics", "Lyrics"),
            ("doLyricsmore", "Lyricsmore"),
            ("doLyricsmoremore", "Lyricsmoremore"),
        ] {
            if enabled(flag) {
                writeln!(
                    ly,
                    "\\new Lyrics \\lyricsto \"Voice\" {{ {} }}",
                    part(&format!("{name}{version}"))?
                )
                .unwrap();
            }
        }
        ly.push_str(">> }\n");
    }
    if enabled("doExtra") {
        ly.push_str(part("Extra")?);
    }

    if let Some(copyright) = get("copyright")? {
        let label = if heb {
            "\u{5d6}\u{5db}\u{5d5}\u{5d9}\u{5d5}\u{5ea} \u{5d9}\u{5d5}\u{5e6}\u{5e8}\u{5d9}\u{5dd} \u{a9} "
        } else {
            "Copyright \u{a9} "
        };
        row(&mut ly, &format!("{label}{copyright}"), false);
    } else {
        let help = if heb {
            "-- \u{5e2}\u{5d9}\u{5d6}\u{5e8}\u{5d5} \u{5dc}\u{5d9} \u{5dc}\u{5de}\u{5dc}\u{5d0} \u{5d0}\u{5ea} \u{5e9}\u{5d5}\u{5e8}\u{5ea} \u{5d6}\u{5db}\u{5d5}\u{5d9}\u{5d5}\u{5ea} \u{5d4}\u{5d9}\u{5d5}\u{5e6}\u{5e8}\u{5d9}\u{5dd} \u{5d4}\u{5d6}\u{5d5} --"
        } else {
            "-- help me fill it out this copyright notice --"
        };
        row(&mut ly, help, false);
    }
    for key in ["copyrightextra", "copyrightextraextra"] {
        if let Some(value) = get(key)? {
            row(&mut ly, &value, false);
        }
    }
    if let Some(value) = get("typesetter")? {
        let label = if heb {
            "\u{5ea}\u{5d5}\u{5d5}\u{5d9} \u{5e2}\u{5dc} \u{5d9}\u{5d3}\u{5d9} "
        } else {
            "Typeset by "
        };
        row(&mut ly, &format!("{label}{value}"), false);
    }
    ly.push_str("}\n");
    Ok(ly)
}
