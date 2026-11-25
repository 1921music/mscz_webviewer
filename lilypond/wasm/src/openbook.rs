// SPDX-License-Identifier: GPL-3.0-only
//! Expand the default lead-sheet version of an OpenBook song. The ordering
//! and labels follow OpenBook's common.ly.weave and scripts/drivers.py.
use crate::{Result, check};
use std::collections::BTreeMap;
use std::fmt::Write;
use tera::{Context, Tera};

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
    let (prefix, rest) = source
        .split_once("{#")
        .ok_or("Expected OpenBook song metadata")?;
    let (metadata, body) = rest
        .split_once("#}")
        .ok_or("Unclosed OpenBook song metadata")?;
    // OpenBook also places imports of shared macros before its metadata.
    let shared_macros = if prefix.is_empty() {
        None
    } else {
        let import = prefix
            .trim()
            .strip_prefix("{%")
            .and_then(|s| s.strip_suffix("%}"))
            .ok_or("Expected a shared macro import before OpenBook song metadata")?;
        let words: Vec<_> = import.trim_matches('-').split_whitespace().collect();
        let [
            "import",
            "\"src/include/defs.ly.tera\"" | "'src/include/defs.ly.tera'",
            "as",
            alias,
        ] = words.as_slice()
        else {
            return Err("Unsupported OpenBook shared macro import".into());
        };
        Some(*alias)
    };
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
    let mut tera = Tera::default();
    let template = "song.ly.tera";
    // Translate the parameterless macros used by the OpenBook templates to
    // Tera 2 components. The actual music and control flow still run in Tera.
    let song;
    let body = if let Some(alias) = shared_macros {
        tera.add_raw_template("src/include/defs.ly.tera", include_str!("defs.ly.tera"))
            .map_err(|error| format!("Could not register OpenBook macros: {error}"))?;
        song = body
            .replace("{% macro chords() %}", "{% component song.chords() %}")
            .replace("{% endmacro %}", "{% endcomponent song.chords %}")
            .replace("{{ self::chords() }}", "{{<song.chords />}}")
            .replace(
                &format!("{{{{ {alias}::chordDefs() }}}}"),
                "{{<defs.chordDefs />}}",
            );
        song.as_str()
    } else {
        body
    };
    tera.add_raw_template(template, body)
        .map_err(|error| format!("Could not compile OpenBook template: {error}"))?;
    let render_part = |name: &str| {
        let mut context = Context::new();
        context.insert("part", name);
        tera.render(template, &context)
            .map_err(|error| format!("Could not render OpenBook part {name}: {error}"))
    };
    let part = |name: &str| {
        let music = render_part(name)?;
        check(
            !music.trim().is_empty(),
            format!("Empty or missing OpenBook part {name}"),
        )?;
        Ok::<_, String>(music)
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
        let definitions = render_part("Defs")?;
        if !definitions.trim().is_empty() {
            ly.push_str(&definitions);
            ly.push('\n');
        }
        ly.push_str(&part("Own")?);
    } else {
        if enabled("doPrep") {
            ly.push_str(&part("Prep")?);
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
        ly.push_str(&part("Extra")?);
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

#[cfg(test)]
mod tests {
    use super::render;

    #[test]
    fn rockbook_macros_expand_as_tera_components() {
        let ly = render(
            r#"{% import "src/include/defs.ly.tera" as shared %}{#
default_version = "Own"
[attributes]
title = "Example"
[versions.Own]
doOwn = true
#}
{% macro chords() %}\chordmode { c1 | }{% endmacro %}
{% if part == "Own" %}\score { << {{ shared::chordDefs() }} {{ self::chords() }} >> }{% endif %}
"#,
        )
        .unwrap();
        assert!(ly.contains(r#"\new ChordNames="Chords""#));
        assert!(ly.contains(r"\chordmode { c1 | }"));
    }
}
