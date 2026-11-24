use std::collections::BTreeMap;
use std::fmt::{Display, Write};

use crate::lilypond::{Score, set_layout};
use crate::model::*;
use crate::score::{Barline, Plan};
use crate::{Result, check};

#[derive(Debug)]
struct Xml {
    name: String,
    attrs: Vec<(&'static str, String)>,
    text: String,
    children: Vec<Xml>,
}

impl Xml {
    fn new(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            attrs: Vec::new(),
            text: String::new(),
            children: Vec::new(),
        }
    }

    fn attr(&mut self, key: &'static str, value: impl Display) -> &mut Self {
        self.attrs.push((key, value.to_string()));
        self
    }

    fn add(&mut self, key: impl Into<String>) -> &mut Self {
        self.children.push(Self::new(key));
        self.children.last_mut().unwrap()
    }

    fn text(&mut self, key: impl Into<String>, text: impl Display) -> &mut Self {
        let value = self.add(key);
        value.text = text.to_string();
        value
    }

    fn write(&self, depth: usize, out: &mut String) {
        out.extend(std::iter::repeat_n(' ', depth * 2));
        out.push('<');
        out.push_str(&self.name);
        for (key, value) in &self.attrs {
            write!(out, " {key}=\"").unwrap();
            escape(value, out, true);
            out.push('"');
        }
        if self.children.is_empty() && self.text.is_empty() {
            out.push_str(" />");
            return;
        }
        out.push('>');
        escape(&self.text, out, false);
        for child in &self.children {
            out.push('\n');
            child.write(depth + 1, out);
        }
        if !self.children.is_empty() {
            out.push('\n');
            out.extend(std::iter::repeat_n(' ', depth * 2));
        }
        out.push_str("</");
        out.push_str(&self.name);
        out.push('>');
    }
}

fn escape(text: &str, out: &mut String, attribute: bool) {
    for ch in text.chars() {
        match ch {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' if attribute => out.push_str("&quot;"),
            '\r' if attribute => out.push_str("&#13;"),
            '\n' if attribute => out.push_str("&#10;"),
            '\t' if attribute => out.push_str("&#09;"),
            ch => out.push(ch),
        }
    }
}

fn known_kind(quality: &str) -> Option<&'static str> {
    Some(match quality {
        "" | "5" | "maj1" => "major",
        "m" => "minor",
        "m5" => "power",
        "7" | "7.9-" | "7.9+" | "7.3-.5-.9-.11-.13-" => "dominant",
        "m7" => "minor-seventh",
        "maj7" | "maj" | "7+" => "major-seventh",
        "m7.5-" => "half-diminished",
        "9" | "7.9" => "dominant-ninth",
        "dim7" => "diminished-seventh",
        "dim" => "diminished",
        "6" | "6.9" => "major-sixth",
        "m7.9" | "m9" => "minor-ninth",
        "maj7.9" | "maj9" => "major-ninth",
        "m11" => "minor-11th",
        "maj11" => "major-11th",
        "m6" => "minor-sixth",
        "m7+" => "major-minor",
        "m13" => "minor-13th",
        "maj13" => "major-13th",
        "5+" | "aug" => "augmented",
        "5+7" | "aug7" => "augmented-seventh",
        "11" => "dominant-11th",
        "13" => "dominant-13th",
        "sus" | "sus4" => "suspended-fourth",
        "sus2" => "suspended-second",
        _ => return None,
    })
}

type Degree = (u8, i8, &'static str);

fn chord_structure(quality: &str) -> Result<(&'static str, Vec<Degree>)> {
    let quality = if quality == "dim5m7" { "7.5-" } else { quality };
    if let Some(kind) = known_kind(quality) {
        let degrees: &[Degree] = match quality {
            "6.9" => &[(9, 0, "add")],
            "7.9-" => &[(9, -1, "add")],
            "7.9+" => &[(9, 1, "add")],
            "7.3-.5-.9-.11-.13-" => &[
                (3, -1, "alter"),
                (5, -1, "alter"),
                (9, -1, "add"),
                (11, -1, "add"),
                (13, -1, "add"),
            ],
            _ => &[],
        };
        return Ok((kind, degrees.to_vec()));
    }
    let (additions, omissions) = quality.split_once('^').unwrap_or((quality, ""));
    let mut terms = additions.split('.');
    let raw = terms.next().unwrap();
    let mut base = raw.to_string();
    let mut modifiers: Vec<String> = terms.map(str::to_string).collect();
    let mut omissions = if omissions.is_empty() {
        Vec::new()
    } else {
        omissions.split('.').collect()
    };
    if let Some((extent, tail)) = raw.split_once("sus") {
        let (suspension, trailing) = match tail.strip_prefix('2') {
            Some(tail) => ("sus2", tail),
            None => ("sus4", tail.strip_prefix('4').unwrap_or(tail)),
        };
        check(
            matches!(trailing, "" | "7"),
            format!("Unsupported chord quality {quality:?}"),
        )?;
        base = suspension.into();
        let prefix: &[&str] = match extent {
            "" if trailing.is_empty() => &[],
            "" | "7" => &["7"],
            "9" => &["7", "9"],
            "9-" => &["7", "9-"],
            "13" => &["7", "9", "13"],
            _ => return Err(format!("Unsupported chord quality {quality:?}")),
        };
        modifiers.splice(0..0, prefix.iter().map(|&s| s.into()));
    } else if raw == "5+9" {
        base = "9".into();
        modifiers.insert(0, "5+".into());
    } else if raw != "3-"
        && known_kind(raw).is_none()
        && raw.ends_with(['+', '-'])
        && raw.len() >= 2
    {
        let prefix = &raw[..raw.len() - 2];
        let degree = &raw[raw.len() - 2..raw.len() - 1];
        if prefix.bytes().all(|c| c.is_ascii_lowercase()) && matches!(degree, "5" | "6" | "9") {
            base = format!("{prefix}{}", if degree == "9" { "7" } else { degree });
            modifiers.insert(0, raw[raw.len() - 2..].into());
        }
    } else if matches!(raw, "3" | "3-") {
        base = if raw == "3-" {
            "m".into()
        } else {
            String::new()
        };
        if !modifiers
            .iter()
            .any(|item| item.trim_end_matches(['+', '-']) == "5")
        {
            omissions.insert(0, "5");
        }
    }
    let kind = known_kind(&base).ok_or_else(|| format!("Unsupported chord quality {quality:?}"))?;
    let mut present = vec![
        match kind {
            "suspended-second" => 2,
            "suspended-fourth" => 4,
            _ => 3,
        },
        5,
    ];
    match kind.rsplit('-').next().unwrap() {
        "sixth" => present.push(6),
        "seventh" => present.push(7),
        "ninth" => present.extend([7, 9]),
        "11th" => present.extend([7, 9, 11]),
        "13th" => present.extend([7, 9, 11, 13]),
        "minor" if kind == "major-minor" => present.push(7),
        _ => {}
    }
    let mut degrees = Vec::<Degree>::new();
    let apply = |degrees: &mut Vec<Degree>, entry: Degree| {
        if let Some(old) = degrees.iter_mut().find(|d| d.0 == entry.0) {
            *old = entry;
        } else {
            degrees.push(entry);
        }
    };
    for token in modifiers {
        let text = token.trim_end_matches(['+', '-']);
        let value = degree_value(text, quality)?;
        let alter = match &token[text.len()..] {
            "" => 0,
            "+" => 1,
            "-" => -1,
            _ => return Err(format!("Unsupported chord quality {quality:?}")),
        };
        if !present.contains(&value) {
            apply(&mut degrees, (value, alter, "add"));
        } else if alter != 0 {
            apply(&mut degrees, (value, alter, "alter"));
        }
    }
    for token in omissions {
        let value = degree_value(token, quality)?;
        if present.contains(&value) {
            apply(&mut degrees, (value, 0, "subtract"));
        } else {
            degrees.retain(|d| d.0 != value);
        }
    }
    Ok((kind, degrees))
}

fn degree_value(text: &str, quality: &str) -> Result<u8> {
    let degree = text.parse::<u8>().ok().filter(|n| matches!(n, 2..=11 | 13));
    degree.ok_or_else(|| format!("Unsupported chord quality {quality:?}"))
}

fn pitch(parent: &mut Xml, prefix: &str, pitch: Pitch) {
    if prefix.is_empty() {
        parent.text("step", pitch.step_name());
        if pitch.alter != 0 {
            parent.text("alter", pitch.alter);
        }
        parent.text("octave", pitch.octave);
    } else {
        parent.text(format!("{prefix}-step"), pitch.step_name());
        if pitch.alter != 0 {
            parent.text(format!("{prefix}-alter"), pitch.alter);
        }
    }
}

fn harmony(bar: &mut Xml, event: &Event, offset: i64, visible: bool) -> Result<()> {
    if event.rest.is_some() {
        if visible {
            let chord = bar.add("harmony");
            chord.text("kind", "none").attr("text", "N.C.");
            if offset != 0 {
                chord.text("offset", offset);
            }
        }
        return Ok(());
    }
    let data = event.harmony.as_ref().ok_or("Missing harmony")?;
    let (kind, degrees) = chord_structure(&data.quality)?;
    let chord = bar.add("harmony");
    if event.alternate_chord {
        chord.attr("type", "alternate");
    }
    if !visible {
        chord.attr("print-object", "no");
    }
    pitch(chord.add("root"), "root", event.members[0].pitch);
    let kind = chord.text("kind", kind);
    if data.quality == "7.3-.5-.9-.11-.13-" {
        kind.attr("text", "7alt");
    }
    if let Some(bass) = data.bass {
        pitch(chord.add("bass"), "bass", bass);
    }
    for (value, alter, operation) in degrees {
        let degree = chord.add("degree");
        degree.text("degree-value", value);
        degree.text("degree-alter", alter);
        degree.text("degree-type", operation);
    }
    if offset != 0 {
        chord.text("offset", offset);
    }
    if let Some(parens) = event.chord_parenthesis {
        chord.text("level", format!("openbook-parenthesis-{parens}"));
    }
    Ok(())
}

fn direction(bar: &mut Xml, d: &Direction, offset: i64) {
    let direction = bar.add("direction");
    if let Some(placement) = d.placement {
        direction.attr("placement", placement);
    }
    let kind = direction.add("direction-type");
    match d.kind {
        "dynamics" => {
            kind.add(d.kind).add(&d.text);
        }
        "segno" => {
            kind.add("other-direction").attr("smufl", d.kind);
        }
        "coda" => {
            kind.add("other-direction")
                .attr("smufl", d.kind)
                .attr("relative-x", "-15")
                .attr("default-y", "43");
        }
        "wedge" => {
            kind.add(d.kind).attr("type", &d.text);
        }
        "octave-shift" => {
            kind.add(d.kind).attr("type", &d.text).attr("size", "8");
        }
        _ => {
            kind.text(d.kind, &d.text);
        }
    }
    if offset != 0 {
        direction.text("offset", offset);
    }
}

fn note_ties(event: &Event, member: Option<&Member>) -> Vec<&'static str> {
    [("stop", &event.tie_stops), ("start", &event.tie_starts)]
        .into_iter()
        .filter_map(|(tag, keys)| member.filter(|m| keys.contains(&m.pitch)).map(|_| tag))
        .collect()
}

fn note_head<'a>(
    bar: &'a mut Xml,
    event: &Event,
    member: Option<&Member>,
    divisions: i64,
    ties: &[&str],
    voice: Option<&str>,
    chord_member: bool,
) -> Result<&'a mut Xml> {
    let note = bar.add("note");
    if event.invisible || event.rest == Some('s') {
        note.attr("print-object", "no");
    }
    if let Some(slash) = event.grace {
        note.add("grace")
            .attr("slash", if slash { "yes" } else { "no" });
    }
    if chord_member {
        note.add("chord");
    }
    if let Some(member) = member {
        pitch(note.add("pitch"), "", member.pitch);
    } else {
        let rest = note.add("rest");
        if event.measure_rest {
            rest.attr("measure", "yes");
        }
    }
    if event.grace.is_none() {
        note.text("duration", event.length().units(divisions));
    }
    for tie in ties {
        note.add("tie").attr("type", tie);
    }
    if let Some(voice) = voice {
        note.text("voice", voice);
    }
    if !event.measure_rest {
        let value = event.written * event.scale;
        let (kind, dots) =
            note_type(value).ok_or_else(|| format!("Unsupported written note value {value}"))?;
        note.text("type", kind);
        for _ in 0..dots {
            note.add("dot");
        }
    }
    if let Some(tuplet) = event.tuplet {
        let modifier = note.add("time-modification");
        modifier.text("actual-notes", tuplet.denominator);
        modifier.text("normal-notes", tuplet.numerator);
    }
    if let Some(member) = member {
        if let Some(stem) = event.stem {
            note.text("stem", stem);
        }
        let value = if member.harmonic {
            "diamond"
        } else {
            event.notehead
        };
        let parenthesized = event.parenthesized || member.parentheses;
        if parenthesized || value != "normal" {
            let head = note.text("notehead", value);
            if parenthesized {
                head.attr("parentheses", "yes");
            }
        }
    }
    if !chord_member {
        for &(number, status) in &event.beams {
            note.text("beam", status).attr("number", number);
        }
    }
    Ok(note)
}

fn note(
    bar: &mut Xml,
    event: &Event,
    divisions: i64,
    lyrics: &Lyrics,
    voice: Option<&str>,
) -> Result<()> {
    for d in &event.directions {
        direction(bar, d, 0);
    }
    let member = event.pitched().then(|| &event.members[0]);
    let ties = note_ties(event, member);
    let open = member.is_some_and(|m| event.open_ties.contains(&m.pitch));
    let first = note_head(bar, event, member, divisions, &ties, voice, false)?;
    if !ties.is_empty()
        || event.repeat_tie
        || event.let_ring
        || open
        || event.glissando_start.is_some()
        || event.glissando_stop.is_some()
        || event.tuplet_start
        || event.tuplet_stop
        || !event.markings.is_empty()
        || !event.slurs.is_empty()
    {
        let marks = first.add("notations");
        for tie in &ties {
            marks.add("tied").attr("type", tie);
        }
        if event.repeat_tie && !ties.contains(&"stop") {
            marks.add("tied").attr("type", "stop");
        }
        if event.let_ring || open {
            marks.add("tied").attr("type", "let-ring");
        }
        for (kind, style) in [
            ("stop", event.glissando_stop),
            ("start", event.glissando_start),
        ] {
            if let Some(style) = style {
                marks
                    .add("slide")
                    .attr("type", kind)
                    .attr("number", "1")
                    .attr(
                        "line-type",
                        if style == "zigzag" { "wavy" } else { "solid" },
                    );
            }
        }
        for &(kind, number, placement) in &event.slurs {
            let slur = marks.add("slur").attr("type", kind).attr("number", number);
            if let Some(placement) = placement.filter(|_| kind == "start") {
                slur.attr("placement", placement);
            }
        }
        if event.tuplet_start {
            marks
                .add("tuplet")
                .attr("type", "start")
                .attr("bracket", if event.tuplet_bracket { "yes" } else { "no" });
        }
        if event.tuplet_stop {
            marks.add("tuplet").attr("type", "stop");
        }
        notation_groups(marks, &event.markings);
    }
    if event.grace.is_none() {
        let key = (event.lyric_voice, event.position);
        for (number, syllable) in lyrics.syllables.get(&key).into_iter().flatten() {
            let lyric = first
                .add("lyric")
                .attr("number", number)
                .attr("placement", "below");
            lyric.text("syllabic", syllable.syllabic());
            let text = lyric.text("text", &syllable.text);
            if syllable.italic {
                text.attr("font-style", "italic");
            }
            if syllable.extend {
                let line = lyric.add("extend");
                if let Some(kind) = syllable.extend_type {
                    line.attr("type", kind);
                }
            }
        }
        for number in lyrics.stops.get(&key).into_iter().flatten() {
            first
                .add("lyric")
                .attr("number", number)
                .attr("placement", "below")
                .add("extend")
                .attr("type", "stop");
        }
    }
    if event.pitched() {
        for member in &event.members[1..] {
            let ties = note_ties(event, Some(member));
            let open = event.open_ties.contains(&member.pitch);
            let arpeggio = event.markings.iter().any(|m| m.value == "arpeggiate");
            let extra = note_head(bar, event, Some(member), divisions, &ties, voice, true)?;
            if !ties.is_empty() || open || arpeggio {
                let marks = extra.add("notations");
                if arpeggio {
                    marks.add("arpeggiate");
                }
                for tie in ties {
                    marks.add("tied").attr("type", tie);
                }
                if open {
                    marks.add("tied").attr("type", "let-ring");
                }
            }
        }
    }
    Ok(())
}

fn notation_groups(parent: &mut Xml, marks: &[Mark]) {
    let mut containers = BTreeMap::new();
    for mark in marks {
        match mark.value.as_str() {
            "fermata" => {
                parent.add("fermata").attr(
                    "type",
                    if mark.placement == Some("below") {
                        "inverted"
                    } else {
                        "upright"
                    },
                );
            }
            "arpeggiate" => {
                parent.add("arpeggiate");
            }
            _ => {
                let key = match mark.group {
                    "bend" => "technical",
                    "tremolo" => "ornaments",
                    _ => mark.group,
                };
                let index = *containers.entry(key).or_insert_with(|| {
                    let index = parent.children.len();
                    parent.add(key);
                    index
                });
                let group = &mut parent.children[index];
                let mark_xml = match mark.group {
                    "technical" => group.text("fingering", &mark.value),
                    "bend" => group.add("bend").text("bend-alter", &mark.value),
                    "tremolo" => group.text("tremolo", &mark.value).attr("type", "single"),
                    _ => group.add(&mark.value),
                };
                if let Some(placement) = mark.placement {
                    mark_xml.attr("placement", placement);
                }
            }
        }
    }
}

fn barline(bar: &mut Xml, location: &str, marks: &Barline) {
    let line = bar.add("barline").attr("location", location);
    if let Some((kind, _)) = marks.repeat {
        line.text(
            "bar-style",
            if kind == "forward" {
                "heavy-light"
            } else {
                "light-heavy"
            },
        );
    } else if marks.final_bar {
        line.text("bar-style", "light-heavy");
    }
    if let Some((number, kind)) = &marks.ending {
        let text = if number.contains(',') && *kind == "start" {
            number
                .split(',')
                .map(|n| format!("{n}."))
                .collect::<Vec<_>>()
                .join(",")
        } else {
            String::new()
        };
        line.text("ending", text)
            .attr("number", number)
            .attr("type", *kind);
    }
    if let Some((kind, times)) = marks.repeat {
        let repeat = line.add("repeat").attr("direction", kind);
        if let Some(times) = times {
            repeat.attr("times", times);
        }
    }
}

fn attributes(
    bar: &mut Xml,
    melody: &Track,
    divisions: i64,
    position: Time,
    rest_count: Option<usize>,
) {
    let first = position == Time::ZERO;
    let key = melody
        .keys
        .get(&position)
        .copied()
        .or(first.then_some((0, "major")));
    let meter = melody
        .meters
        .get(&position)
        .copied()
        .or(first.then(Meter::default));
    let clef = melody
        .clefs
        .get(&position)
        .copied()
        .or(first.then_some(("G", 2, 0)));
    if !first && key.is_none() && meter.is_none() && clef.is_none() && rest_count.is_none() {
        return;
    }
    let attrs = bar.add("attributes");
    if first {
        attrs.text("divisions", divisions);
    }
    if let Some((fifths, mode)) = key {
        let key = attrs.add("key");
        key.text("fifths", fifths);
        key.text("mode", mode);
    }
    if let Some(m) = meter {
        let time = attrs.add("time");
        match (m.beats, m.beat_type, m.numeric) {
            (4, 4, false) => {
                time.attr("symbol", "common");
            }
            (2, 2, false) => {
                time.attr("symbol", "cut");
            }
            _ => {}
        }
        if m.hidden {
            time.attr("print-object", "no");
        }
        time.text("beats", m.beats);
        time.text("beat-type", m.beat_type);
    }
    if let Some((sign, line, shift)) = clef {
        let clef = attrs.add("clef");
        clef.text("sign", sign);
        clef.text("line", line);
        if shift != 0 {
            clef.text("clef-octave-change", shift);
        }
    }
    if first
        && let Some((diatonic, chromatic, octave)) = melody.transposition
        && (diatonic, chromatic, octave) != (0, 0, 0)
    {
        let transpose = attrs.add("transpose");
        transpose.text("diatonic", diatonic);
        transpose.text("chromatic", chromatic);
        if octave != 0 {
            transpose.text("octave-change", octave);
        }
    }
    if let Some(rest_count) = rest_count.filter(|&n| n != 0) {
        attrs.add("measure-style").text("multiple-rest", rest_count);
    }
}

fn tempo(bar: &mut Xml, tempo: &Tempo, offset: i64) -> Result<()> {
    if !tempo.has_text && tempo.bpm.is_none() {
        return Ok(());
    }
    let direction = bar.add("direction").attr("placement", "above");
    if let Some(words) = &tempo.words {
        let value = if tempo.bpm.is_some() {
            format!("{words} ")
        } else {
            words.clone()
        };
        direction.add("direction-type").text("words", value);
    }
    if let Some((bpm, _)) = tempo.bpm {
        let metro = direction
            .add("direction-type")
            .add("metronome")
            .attr("parentheses", if tempo.has_text { "yes" } else { "no" });
        let (kind, dots) = note_type(tempo.beat).ok_or("Unsupported tempo beat unit")?;
        metro.text("beat-unit", kind);
        for _ in 0..dots {
            metro.add("beat-unit-dot");
        }
        metro.text("per-minute", bpm);
    }
    if offset != 0 {
        direction.text("offset", offset);
    }
    if let Some((bpm, _)) = tempo.bpm {
        let ratio = tempo.beat * Time::integer(4 * bpm);
        let bpm = ratio.numerator as f64 / ratio.denominator as f64;
        direction.add("sound").attr("tempo", format!("{bpm:?}"));
    }
    Ok(())
}

fn wrap(text: &str, width: usize) -> String {
    let mut chars = Vec::new();
    let mut column = 0;
    for ch in text.chars() {
        match ch {
            '\t' => {
                let spaces = 8 - column % 8;
                chars.extend(std::iter::repeat_n(' ', spaces));
                column += spaces;
            }
            '\n' | '\r' => {
                chars.push(' ');
                column = 0;
            }
            '\x0b' | '\x0c' => {
                chars.push(' ');
                column += 1;
            }
            _ => {
                chars.push(ch);
                column += 1;
            }
        }
    }

    let mut chunks = Vec::new();
    let mut start = 0;
    let is_word = |c: char| c.is_alphanumeric() || c == '_';
    let is_letter = |c: char| c.is_alphabetic() || c == '_';
    while start < chars.len() {
        let mut end = start + 1;
        if chars[start] == ' ' {
            while chars.get(end) == Some(&' ') {
                end += 1;
            }
        } else if chars[start] == '-' && chars.get(end) == Some(&'-') {
            while chars.get(end) == Some(&'-') {
                end += 1;
            }
            if !chars.get(end).copied().is_some_and(is_word)
                || start == 0
                || !(is_word(chars[start - 1]) || "!\"'&.,?".contains(chars[start - 1]))
            {
                while chars.get(end).is_some_and(|&c| c != ' ') {
                    end += 1;
                }
            }
        } else {
            while chars.get(end).is_some_and(|&c| c != ' ') {
                if chars[end] == '-'
                    && chars.get(end + 1) == Some(&'-')
                    && (is_word(chars[end - 1]) || "!\"'&.,?".contains(chars[end - 1]))
                    && chars[end..]
                        .iter()
                        .find(|&&c| c != '-')
                        .copied()
                        .is_some_and(is_word)
                {
                    break;
                }
                if chars[end] == '-'
                    && end >= 2
                    && is_letter(chars[end - 1])
                    && (is_letter(chars[end - 2])
                        || (end >= 3 && chars[end - 2] == '-' && is_letter(chars[end - 3])))
                    && chars.get(end + 1).copied().is_some_and(is_letter)
                    && chars
                        .get(
                            end + if chars.get(end + 2) == Some(&'-') {
                                3
                            } else {
                                2
                            },
                        )
                        .copied()
                        .is_some_and(is_letter)
                {
                    end += 1;
                    break;
                }
                end += 1;
            }
        }
        chunks.push(&chars[start..end]);
        start = end;
    }
    chunks.reverse();

    let mut lines = Vec::new();
    while let Some(&next) = chunks.last() {
        if !lines.is_empty() && next.iter().all(|c| c.is_whitespace()) {
            chunks.pop();
        }
        let mut line = Vec::new();
        let mut used = 0;
        while let Some(&next) = chunks.last() {
            if used + next.len() > width {
                if next.len() > width {
                    let available = width - used;
                    let end = next[..available]
                        .iter()
                        .rposition(|&c| c == '-')
                        .filter(|&n| n > 0 && next[..n].iter().any(|&c| c != '-'))
                        .map_or(available, |n| n + 1);
                    if end != 0 {
                        line.push(&next[..end]);
                        *chunks.last_mut().unwrap() = &next[end..];
                    }
                }
                break;
            }
            used += next.len();
            line.push(chunks.pop().unwrap());
        }
        if line
            .last()
            .is_some_and(|word| word.iter().all(|c| c.is_whitespace()))
        {
            line.pop();
        }
        if !line.is_empty() {
            lines.push(line.into_iter().flatten().collect::<String>());
        }
    }
    lines.join("\n")
}

fn credit(root: &mut Xml, kind: &str, lines: &[(String, u8)], y: i32) {
    let credit = root.add("credit").attr("page", "1");
    credit.text("credit-type", kind);
    for (i, (text, size)) in lines.iter().enumerate() {
        credit
            .text(
                "credit-words",
                format!("{}{text}", if i == 0 { "" } else { "\n\n" }),
            )
            .attr("default-x", "600")
            .attr("default-y", y)
            .attr("justify", "center")
            .attr("halign", "center")
            .attr("font-size", size);
    }
}

fn info(root: &mut Xml, info: &Info, title: Option<&str>, lyrics: &Lyrics) {
    let title = title.filter(|s| !s.is_empty()).or(info.title.as_deref());
    if let Some(title) = title.filter(|s| !s.is_empty()) {
        root.text("movement-title", title);
    }
    let id = root.add("identification");
    for (role, value) in [
        ("composer", &info.composer),
        ("lyricist", &info.lyricist),
        ("engraver", &info.engraver),
    ] {
        if let Some(value) = value {
            id.text("creator", value).attr("type", role);
        }
    }
    let mut footer: Vec<_> = info.rights.iter().map(|text| wrap(text, 108)).collect();
    if let Some(engraver) = &info.engraver {
        footer.push(format!("Typeset by {engraver}"));
    }
    if !footer.is_empty() {
        id.text("rights", footer.join("\n"));
    }
    id.add("encoding")
        .text("software", "mscz_webviewer LilyPond prototype");
    let openbook = info.title.is_some();
    let mut details = Vec::new();
    if openbook || !lyrics.unplaced.is_empty() {
        let fields = id.add("miscellaneous");
        for (number, suffix) in &lyrics.unplaced {
            fields
                .text("miscellaneous-field", suffix.join(" "))
                .attr("name", format!("openbook-unplaced-verse-{number}"));
        }
        if let Some(subtitle) = &info.subtitle {
            fields
                .text("miscellaneous-field", subtitle)
                .attr("name", "subtitle");
            details.push((subtitle.clone(), 14));
        }
        if info.composer.is_some() && info.composer == info.lyricist {
            details.push((
                format!("{} (lyrics and music)", info.composer.as_deref().unwrap()),
                10,
            ));
        } else {
            let people: Vec<_> = [(&info.lyricist, "lyrics"), (&info.composer, "music")]
                .into_iter()
                .filter_map(|(value, label)| {
                    value.as_ref().map(|value| format!("{value} ({label})"))
                })
                .collect();
            if !people.is_empty() {
                details.push((people.join("    "), 10));
            }
        }
        if !info.headings.is_empty() {
            fields
                .text("miscellaneous-field", info.headings.join("\n"))
                .attr("name", "piece");
            details.extend(info.headings.iter().map(|text| (text.clone(), 10)));
        }
    }
    if openbook {
        let defaults = root.add("defaults");
        let scaling = defaults.add("scaling");
        scaling.text("millimeters", "5.5");
        scaling.text("tenths", "40");
        let page = defaults.add("page-layout");
        page.text("page-height", "2160.000");
        page.text("page-width", "1527.273");
        let margins = page.add("page-margins").attr("type", "both");
        for (tag, value) in [
            ("left-margin", "80.000"),
            ("right-margin", "80.000"),
            ("top-margin", "58.182"),
            ("bottom-margin", "58.182"),
        ] {
            margins.text(tag, value);
        }
        defaults.add("system-layout").text("system-distance", "30");
        if !lyrics.syllables.is_empty() {
            defaults.add("lyric-font").attr("font-size", "9");
        }
        credit(root, "title", &[(title.unwrap_or("").into(), 22)], 1950);
        if !details.is_empty() {
            credit(root, "subtitle", &details, 1910);
        }
    }
}

#[derive(PartialEq)]
enum ChordSignature<'a> {
    Rest,
    Pitched(usize, i8, &'a str, Option<(bool, usize, i8)>),
}

fn signature(event: &Event) -> ChordSignature<'_> {
    if event.rest.is_some() {
        ChordSignature::Rest
    } else {
        let root = event.members[0].pitch;
        let data = event.harmony.as_ref().unwrap();
        let (spec, plus_bass) = data
            .spec
            .split_once('/')
            .map_or((data.spec.as_str(), false), |(prefix, bass)| {
                (prefix, bass.starts_with('+'))
            });
        ChordSignature::Pitched(
            root.step,
            root.alter,
            spec,
            data.bass.map(|p| (plus_bass, p.step, p.alter)),
        )
    }
}

pub fn write(
    score: &Score<'_, '_>,
    plan: &Plan,
    title: Option<&str>,
    measure_numbers: Option<&str>,
) -> Result<String> {
    let melody = &score.melody;
    let chords = &score.chords;
    let divisions = plan.divisions;
    let mut root = Xml::new("score-partwise");
    root.attr("version", "4.0");
    info(&mut root, &score.info, title, &plan.lyrics);
    root.add("part-list")
        .add("score-part")
        .attr("id", "P1")
        .add("part-name");
    let part = root.add("part").attr("id", "P1");
    let mut chord_events = chords
        .events
        .iter()
        .filter(|event| event.rest != Some('s'))
        .peekable();
    let mut melody_events = melody.events.iter().peekable();
    let mut previous_chord = None;
    let mut meter_length = Time::ONE;
    let primary = (!melody.secondary.is_empty()).then_some("1");
    for (index, boundary) in plan.boundaries.windows(2).enumerate() {
        let (start, end) = (boundary[0], boundary[1]);
        if let Some(meter) = melody.meters.get(&start) {
            meter_length = meter.length();
        }
        let bar = part
            .add("measure")
            .attr("number", index as i64 + plan.pickup_number);
        if end - start != meter_length {
            bar.attr("implicit", "yes");
        }
        if index > 0 {
            let mut layout = plan.breaks.get(&start).cloned().unwrap_or_default();
            if plan.fixed_systems.contains(&index)
                && !layout.iter().any(|&(k, _)| k == "new-system")
            {
                set_layout(&mut layout, ("new-system", "no"));
            }
            if !layout.is_empty() {
                let print = bar.add("print");
                for &(key, value) in &layout {
                    print.attr(key, value);
                }
            }
            if layout.contains(&("new-system", "yes")) || layout.contains(&("new-page", "yes")) {
                previous_chord = None;
            }
        } else if let Some(numbers) = measure_numbers {
            bar.add("print").text("measure-numbering", numbers);
        }
        attributes(
            bar,
            melody,
            divisions,
            start,
            plan.rest_counts.get(&index).copied(),
        );
        for (&position, mark) in melody.tempos.range(start..end) {
            tempo(bar, mark, (position - start).units(divisions))?;
        }
        if let Some(line) = plan.left.get(&start)
            && (start != Time::ZERO || score.info.title.is_none())
        {
            barline(bar, "left", line);
        }
        if let Some(mark) = chords.marks.get(&start) {
            bar.add("direction")
                .attr("placement", "above")
                .add("direction-type")
                .text("rehearsal", mark);
        }
        for &mark in chords.navigation.get(&start).into_iter().flatten() {
            direction(bar, &Direction::new(mark, Some("above"), ""), 0);
        }
        for track in [melody, chords] {
            for (&position, directions) in track.directions.range(start..=end) {
                if position < end || end == melody.position {
                    for d in directions {
                        direction(bar, d, (position - start).units(divisions));
                    }
                }
            }
        }
        while let Some(chord) = chord_events.next_if(|event| event.position < end) {
            let current = signature(chord);
            let visible = !chords.chord_changes
                || previous_chord.as_ref() != Some(&current)
                || chord.alternate_chord;
            harmony(
                bar,
                chord,
                (chord.position - start).units(divisions),
                visible,
            )?;
            previous_chord = Some(current);
        }
        let mut cursor = start;
        while let Some(event) = melody_events.next_if(|event| event.position < end) {
            if event.position > cursor {
                bar.add("forward")
                    .text("duration", (event.position - cursor).units(divisions));
            }
            cursor = event.position + event.length();
            check(
                cursor <= end,
                "A melody note crosses a barline without a tie",
            )?;
            note(bar, event, divisions, &plan.lyrics, primary)?;
        }
        if cursor < end {
            bar.add("forward")
                .text("duration", (end - cursor).units(divisions));
            cursor = end;
        }
        let mut secondary = melody
            .secondary
            .iter()
            .filter(|e| start <= e.position && e.position < end)
            .peekable();
        if secondary.peek().is_some() {
            if cursor > start {
                bar.add("backup")
                    .text("duration", (cursor - start).units(divisions));
            }
            cursor = start;
            for event in secondary {
                if event.position > cursor {
                    bar.add("forward")
                        .text("duration", (event.position - cursor).units(divisions));
                }
                cursor = event.position + event.length();
                check(cursor <= end, "Second voice note crosses a barline")?;
                note(bar, event, divisions, &plan.lyrics, Some("2"))?;
            }
        }
        if let Some(line) = plan.right.get(&end) {
            barline(bar, "right", line);
        }
    }
    let mut document = String::from("<?xml version='1.0' encoding='utf-8'?>\n");
    root.write(0, &mut document);
    Ok(document)
}
