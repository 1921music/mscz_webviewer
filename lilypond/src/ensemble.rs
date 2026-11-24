// Grouped notation shares the lead-sheet reader, meter planner and XML writer.
use std::collections::BTreeMap;

use crate::lilypond::{self, Score};
use crate::model::{Time, Track, gcd};
use crate::syntax::{self, Cursor, Group, Kind, Node};
use crate::{Options, Result, check, musicxml, score};

type Definitions<'s> = BTreeMap<&'s str, Vec<Node<'s>>>;

fn grouped<'s>(kind: Group, nodes: Vec<Node<'s>>, anchor: &Node<'s>) -> Node<'s> {
    Node {
        kind: Kind::Group(kind, nodes),
        start: anchor.start,
        end: anchor.end,
    }
}

fn definitions<'s>(nodes: &[Node<'s>], before: usize) -> Result<Definitions<'s>> {
    let mut result = BTreeMap::new();
    let mut cursor = Cursor::new(nodes);
    while let Some(node) = cursor.next().filter(|node| node.start < before) {
        if node.word() == "\\parallelMusic" {
            let names = cursor
                .take("parallelMusic voice list")?
                .scheme()
                .and_then(|word| word.strip_prefix("#'("))
                .and_then(|word| word.strip_suffix(')'))
                .ok_or("Expected parallelMusic voice names")?
                .split_whitespace()
                .collect::<Vec<_>>();
            check(!names.is_empty(), "Empty parallelMusic voice list")?;
            let music = cursor.take("parallelMusic measures")?;
            let bars = music
                .children(Group::Sequence)
                .ok_or("Expected parallelMusic measures")?;
            let mut voices = vec![Vec::new(); names.len()];
            let chunks = bars.split_inclusive(|n| n.word() == "|");
            let count = chunks.clone().count();
            check(count % names.len() == 0, "Incomplete parallelMusic measure")?;
            for (index, bar) in chunks.enumerate() {
                voices[index % names.len()].extend_from_slice(bar);
            }
            for (name, voice) in names.into_iter().zip(voices) {
                result.insert(name, vec![grouped(Group::Sequence, voice, music)]);
            }
        } else if !node.word().is_empty() && cursor.eat("=") {
            let start = cursor.index;
            while let Some(value) = cursor.next().filter(|n| n.start < before) {
                if matches!(
                    value.kind,
                    Kind::Group(Group::Sequence | Group::Simultaneous, _)
                ) {
                    result.insert(node.word(), nodes[start..cursor.index].to_vec());
                    break;
                }
            }
        }
    }
    Ok(result)
}

fn expand<'s>(nodes: &[Node<'s>], defs: &Definitions<'s>, depth: u8) -> Result<Vec<Node<'s>>> {
    check(
        depth <= 32,
        "Recursive or deeply nested LilyPond definition",
    )?;
    let mut output = Vec::new();
    for node in nodes {
        if let Some(definition) = node
            .word()
            .strip_prefix('\\')
            .and_then(|name| defs.get(name))
        {
            // Preserve relative wrappers and keep each reference one expression.
            if definition
                .first()
                .is_some_and(|n| n.word() == "\\lyricmode")
            {
                output.extend(expand(definition, defs, depth + 1)?);
            } else {
                output.push(grouped(
                    Group::Sequence,
                    expand(definition, defs, depth + 1)?,
                    node,
                ));
            }
        } else if let Kind::Group(kind, children) = &node.kind {
            let children = expand(children, defs, depth + 1)?;
            let solo = children.len() == 1
                || matches!(children.as_slice(), [command, body]
                    if matches!(command.word(), "\\drummode" | "\\drums") && body.children(Group::Sequence).is_some())
                || matches!(children.as_slice(), [command, _, body]
                    if command.word() == "\\relative" && body.children(Group::Sequence).is_some());
            let kind = if *kind == Group::Simultaneous && solo {
                Group::Sequence
            } else {
                *kind
            };
            output.push(grouped(kind, children, node));
        } else {
            output.push(node.clone());
        }
    }
    Ok(output)
}

fn staff_group<'s>(
    nodes: &[Node<'s>],
    document: &[Node<'s>],
    setup: &mut Track,
) -> Result<Vec<Track>> {
    let mut cursor = Cursor::new(nodes);
    let mut staves = Vec::new();
    while let Some(node) = cursor.next() {
        match &node.kind {
            Kind::Group(Group::Sequence | Group::Simultaneous, children) => {
                staves.extend(staff_group(children, document, setup)?);
            }
            _ if matches!(node.word(), "\\new" | "\\context") => {
                let (kind, _) = lilypond::context_head(&mut cursor)?;
                check(kind == "Staff", format!("Unsupported {kind} in PianoStaff"))?;
                let body = cursor.take("staff music")?;
                staves.push(lilypond::read_track(
                    std::slice::from_ref(body),
                    document,
                    false,
                )?);
            }
            _ => {
                let start = cursor.index - 1;
                // Read the consecutive score-wide settings in this container.
                while cursor.peek().is_some_and(|n| {
                    !matches!(n.kind, Kind::Group(_, _))
                        && !matches!(n.word(), "\\new" | "\\context")
                }) {
                    cursor.next();
                }
                let settings = lilypond::read_track(&nodes[start..cursor.index], document, false)?;
                check(
                    settings.events.is_empty(),
                    "Expected only settings before PianoStaff staves",
                )?;
                setup.meters.extend(settings.meters);
                setup.keys.extend(settings.keys);
                setup.tempos.extend(settings.tempos);
            }
        }
    }
    Ok(staves)
}

pub fn convert(nodes: &[Node<'_>], options: &Options<'_>) -> Result<Option<String>> {
    if !syntax::contains(nodes, "\\score") {
        return Ok(None);
    }
    let mut scores = Vec::new();
    lilypond::collect_scores(nodes, None, &mut scores)?;
    let printable = scores
        .iter()
        .copied()
        .filter(|(score, _)| {
            score.iter().any(|node| node.word() == "\\layout")
                || !score.iter().any(|node| node.word() == "\\midi")
        })
        .collect::<Vec<_>>();
    if printable.len() <= 1 {
        if let Some(&(body, book)) = printable.first()
            && grouped_score(body)
        {
            return build_grouped(nodes, body, book, options)
                .map(musicxml::serialize)
                .map(Some);
        }
        return Ok(None);
    }

    let mut sections = Vec::new();
    let mut after = 0;
    for (body, book) in printable {
        let before = body.first().map_or(0, |n| n.start);
        let title = section_heading(book.unwrap_or(nodes), after, before)?;
        let xml = if grouped_score(body) {
            build_grouped(nodes, body, book, options)?
        } else {
            let mut score = lilypond::read_score_block(nodes, (body, book))?;
            let plan = score::plan(&mut score, &[])?;
            musicxml::document(&score, &plan, options.title, options.measure_numbers, true)?
        };
        sections.push((title, xml));
        after = body.last().map_or(before, |n| n.end);
    }
    musicxml::join_sections(sections).map(Some)
}

fn grouped_score(body: &[Node<'_>]) -> bool {
    let top = body
        .first()
        .and_then(|n| n.children(Group::Simultaneous))
        .unwrap_or(body);
    syntax::contains(body, "PianoStaff")
        || syntax::contains(body, "DrumStaff")
        || syntax::contains(body, "RhythmicStaff")
        || syntax::contains(body, "FretBoards")
        || has_polyphonic_staff(body)
        || top
            .iter()
            .any(|n| matches!(n.word(), "\\drums" | "\\chordmode"))
}

fn has_polyphonic_staff(nodes: &[Node<'_>]) -> bool {
    nodes.iter().any(|node| match &node.kind {
        Kind::Group(kind, children) => {
            (*kind == Group::Simultaneous
                && children
                    .windows(2)
                    .filter(|pair| {
                        matches!(pair[0].word(), "\\new" | "\\context") && pair[1].word() == "Voice"
                    })
                    .count()
                    > 2)
                || has_polyphonic_staff(children)
        }
        _ => false,
    })
}

fn section_heading(nodes: &[Node<'_>], after: usize, before: usize) -> Result<Option<String>> {
    let mut cursor = Cursor::new(nodes);
    let mut lines = Vec::new();
    while let Some(node) = cursor.next().filter(|node| node.start < before) {
        if node.start >= after && syntax::is_markup(node.word()) {
            let markup = syntax::markup_argument(&mut cursor)?;
            if !syntax::contains(markup, "\\fill-line") {
                let text = syntax::markup_text(markup);
                if !text.trim().is_empty() {
                    lines.push(text.trim().to_owned());
                }
            }
        }
    }
    Ok((!lines.is_empty()).then(|| lines.join("\n")))
}

fn midi_tempo(body: &[Node<'_>]) -> Option<f64> {
    fn tempo_setting(nodes: &[Node<'_>]) -> Option<f64> {
        for (index, node) in nodes.iter().enumerate() {
            if node.word() == "tempoWholesPerMinute" && nodes.get(index + 1)?.word() == "=" {
                let value = nodes
                    .get(index + 2)?
                    .scheme()?
                    .strip_prefix("#(ly:make-moment")?
                    .strip_suffix(')')?
                    .trim();
                let per_minute = Time::parse(value)?;
                if per_minute > Time::ZERO {
                    return Some(4.0 * per_minute.numerator as f64 / per_minute.denominator as f64);
                }
            }
            if let Kind::Group(_, children) = &node.kind
                && let Some(bpm) = tempo_setting(children)
            {
                return Some(bpm);
            }
        }
        None
    }
    body.windows(2)
        .find(|pair| pair[0].word() == "\\midi")
        .and_then(|pair| pair[1].children(Group::Sequence))
        .and_then(tempo_setting)
}

fn build_grouped<'s>(
    nodes: &[Node<'s>],
    body: &[Node<'s>],
    book: Option<&[Node<'s>]>,
    options: &Options<'_>,
) -> Result<musicxml::Xml> {
    let source = book.unwrap_or(nodes);
    let before = body.first().map_or(0, |node| node.start);
    let expanded = expand(body, &definitions(source, before)?, 0)?;
    let music = expanded.first().ok_or("Empty printable score")?;
    let music = music
        .children(Group::Simultaneous)
        .or_else(|| music.children(Group::Sequence))
        .unwrap_or_else(|| {
            let end = expanded
                .iter()
                .position(|node| matches!(node.word(), "\\layout" | "\\midi"))
                .unwrap_or(expanded.len());
            &expanded[..end]
        });
    let mut cursor = Cursor::new(music);
    let mut parts = Vec::new();
    let mut chords = None;
    let mut fretboards = None;
    let mut lyrics = Vec::new();
    let mut named_voices = Vec::new();
    let mut hide_barlines = false;
    while cursor.peek().is_some() {
        if matches!(cursor.peek_word(), "\\drums" | "\\chordmode") {
            let start = cursor.index;
            let command = cursor.take("music mode")?.word();
            cursor.take("music after mode command")?;
            let is_chord = command == "\\chordmode";
            let mut track =
                lilypond::read_track(&cursor.nodes[start..cursor.index], nodes, is_chord)?;
            if is_chord {
                check(chords.replace(track).is_none(), "Expected one chord stream")?;
            } else {
                track.clefs.insert(Time::ZERO, ("percussion", 3, 0));
                parts.push(("Drums", vec![track]));
            }
            continue;
        }
        check(
            cursor.eat("\\new") || cursor.eat("\\context"),
            "Expected a score context",
        )?;
        let head = cursor.index;
        let (kind, name) = lilypond::context_head(&mut cursor)?;
        let settings = cursor.nodes[head..cursor.index]
            .iter()
            .filter_map(|node| node.children(Group::Sequence))
            .flatten();
        match kind {
            "Lyrics" => {
                check(cursor.eat("\\lyricsto"), "Expected a named lyrics voice")?;
                let voice = cursor.take("lyrics voice")?;
                let voice = voice.text().unwrap_or(voice.word());
                cursor.eat("\\lyricmode");
                let words = cursor
                    .take("lyrics music")?
                    .children(Group::Sequence)
                    .ok_or("Expected lyrics")?;
                lyrics.push((voice, words));
            }
            "Voice" | "Staff" | "ChordNames" | "PianoStaff" | "DrumStaff" | "RhythmicStaff"
            | "FretBoards" => {
                while kind == "ChordNames"
                    && cursor.peek_word() == "\\new"
                    && cursor
                        .nodes
                        .get(cursor.index + 1)
                        .is_some_and(|n| n.word() == "ChordNames")
                {
                    cursor.next();
                    lilypond::context_head(&mut cursor)?;
                }
                let start = cursor.index;
                let expression = cursor.take("context music")?;
                if matches!(
                    expression.word(),
                    "\\chordmode" | "\\drummode" | "\\drums" | "\\notemode"
                ) {
                    cursor.take("music after mode command")?;
                } else if expression.word() == "\\relative" {
                    if cursor.peek().is_some_and(|node| !node.word().is_empty()) {
                        cursor.next();
                    }
                    cursor.take("relative music")?;
                }
                let music = &cursor.nodes[start..cursor.index];
                match kind {
                    "PianoStaff" => {
                        let mut setup = Track::default();
                        let mut staves = staff_group(music, nodes, &mut setup)?;
                        check(!staves.is_empty(), "Empty PianoStaff")?;
                        for staff in &mut staves {
                            for (&position, &key) in &setup.keys {
                                staff.keys.entry(position).or_insert(key);
                            }
                            for (&position, &meter) in &setup.meters {
                                staff.meters.entry(position).or_insert(meter);
                            }
                        }
                        staves[0].tempos.extend(setup.tempos);
                        parts.push(("Piano", staves));
                    }
                    "DrumStaff" => {
                        let music = match music {
                            [group] => group
                                .children(Group::Simultaneous)
                                .filter(|children| {
                                    children.iter().filter(|n| n.word() == "\\new").count() == 1
                                })
                                .unwrap_or(music),
                            _ => music,
                        };
                        let mut staff = lilypond::read_track(music, nodes, false)?;
                        let settings = settings.collect::<Vec<_>>();
                        let removed = |engraver| {
                            settings.windows(2).any(|nodes| {
                                nodes[0].word() == "\\remove" && nodes[1].word() == engraver
                            })
                        };
                        hide_barlines |= removed("Bar_engraver");
                        if removed("Time_signature_engraver") {
                            for meter in staff.meters.values_mut() {
                                meter.hidden = true;
                            }
                        }
                        staff.clefs.insert(Time::ZERO, ("percussion", 3, 0));
                        check(
                            staff
                                .events
                                .iter()
                                .chain(&staff.secondary)
                                .flat_map(|event| &event.members)
                                .all(|member| member.drum.is_some()),
                            "Pitched notes in DrumStaff",
                        )?;
                        parts.push(("Drums", vec![staff]));
                    }
                    "RhythmicStaff" => {
                        let track = lilypond::read_track(music, nodes, false)?;
                        parts.push(("Rhythm", vec![track]));
                    }
                    "FretBoards" => {
                        let track = lilypond::read_track(music, nodes, true)?;
                        check(
                            fretboards.replace(track).is_none(),
                            "Expected one FretBoards context",
                        )?;
                    }
                    "ChordNames" => {
                        check(chords.is_none(), "Expected one ChordNames context")?;
                        chords = Some(lilypond::read_track(music, nodes, true)?);
                    }
                    _ => {
                        if let Some(name) = name.filter(|_| kind == "Voice") {
                            named_voices.push(name);
                        }
                        let pitch_squash = settings
                            .clone()
                            .any(|node| node.text() == Some("Pitch_squash_engraver"));
                        let label = if pitch_squash {
                            "Rhythmic Voice"
                        } else {
                            "Voice"
                        };
                        parts.push((label, vec![lilypond::read_track(music, nodes, false)?]));
                    }
                }
            }
            _ => return Err(format!("Unsupported context in grouped score: {kind}")),
        }
    }
    if let Some(fretboards) = fretboards {
        let chords = chords
            .as_mut()
            .ok_or("FretBoards needs a matching chord stream")?;
        chords.guitar_frames = crate::fretboard::align(chords, &fretboards, source, before)?;
        if parts.is_empty() {
            let mut melody = Track {
                position: chords.position,
                pickup: chords.pickup,
                repeats: chords.repeats.clone(),
                meters: chords.meters.clone(),
                ..Track::default()
            };
            melody.events = chords
                .events
                .iter()
                .filter(|event| event.length() != Time::ZERO)
                .map(|event| {
                    let mut placeholder = event.clone();
                    placeholder.invisible = true;
                    placeholder
                })
                .collect();
            parts.push(("Guitar", vec![melody]));
        }
    }
    check(!parts.is_empty(), "No staves in grouped score")?;
    check(
        lyrics
            .iter()
            .all(|(voice, _)| named_voices.as_slice() == [*voice]),
        "Unsupported lyrics target in grouped score",
    )?;

    // Meter changes belong to the shared score timeline even when written only
    // in the vocal line. An explicit setting on a staff takes precedence there.
    let meters = parts
        .iter()
        .flat_map(|(_, staves)| staves)
        .flat_map(|staff| staff.meters.clone())
        .collect::<BTreeMap<_, _>>();
    let end = parts
        .iter()
        .flat_map(|(_, staves)| staves)
        .map(|staff| staff.position)
        .chain(chords.as_ref().map(|track| track.position))
        .max()
        .unwrap();
    let metadata = book.map_or_else(Default::default, lilypond::read_info);
    let mut planned = Vec::new();
    for (name, staves) in parts {
        let mut part = Vec::new();
        for mut melody in staves {
            for (&pos, &meter) in &meters {
                melody.meters.entry(pos).or_insert(meter);
            }
            melody.position = end;
            let first = planned.is_empty() && part.is_empty();
            let mut score = Score {
                chords: if first { chords.take() } else { None }.unwrap_or_else(|| Track {
                    position: end,
                    pickup: melody.pickup,
                    repeats: melody.repeats.clone(),
                    ..Track::default()
                }),
                melody,
                info: if first {
                    metadata.clone()
                } else {
                    Default::default()
                },
                lyric_sections: if first { lyrics.clone() } else { Vec::new() },
                named_voices: if first {
                    named_voices.clone()
                } else {
                    Vec::new()
                },
            };
            let plan = score::plan(&mut score, if first { options.page_breaks } else { &[] })?;
            part.push((score, plan));
        }
        planned.push((name, part));
    }
    let first = &planned[0].1[0].1.boundaries;
    let plans = planned
        .iter()
        .flat_map(|(_, staves)| staves.iter().map(|(_, plan)| plan));
    let mut divisions = 1_i64;
    for plan in plans {
        check(
            plan.boundaries == *first,
            "Staves do not share measure boundaries",
        )?;
        divisions = divisions
            .checked_mul(plan.divisions / gcd(divisions, plan.divisions))
            .ok_or("Too many rhythmic divisions")?;
    }
    for (_, part) in &mut planned {
        for (_, plan) in part {
            plan.divisions = divisions;
        }
    }
    let mut xml = musicxml::build_grouped(&planned, options.title, options.measure_numbers)?;
    if hide_barlines {
        musicxml::hide_barlines(&mut xml);
    }
    if let Some(bpm) = midi_tempo(body) {
        musicxml::set_initial_playback_tempo(&mut xml, bpm);
    }
    Ok(xml)
}

#[cfg(test)]
mod tests {
    use crate::{Options, convert};

    #[test]
    fn prints_two_piano_staves_with_separate_vocals_and_harmony() {
        let xml = convert(
            r#"\score {
                <<
                    \new Voice="singer" { c1 }
                    \new Lyrics \lyricsto "singer" { one }
                    \new PianoStaff <<
                        \new Staff { e1 }
                        \new Staff { \clef bass c1 }
                    >>
                    \new ChordNames \chordmode { c1 }
                >>
                \layout {}
            }
            \score { \new Staff { c1 } \midi {} }"#,
            Options::default(),
        )
        .unwrap();

        assert_eq!(xml.matches("<part id=").count(), 2);
        assert!(xml.contains("<staves>2</staves>"));
        assert!(xml.contains("<clef number=\"2\">"));
        assert!(xml.contains("<staff>2</staff>"));
        assert!(xml.contains("<backup>"));
        assert!(xml.contains("<lyric ") && xml.contains("<text>one</text>"));
        assert!(xml.contains("<harmony>"));
    }

    #[test]
    fn writes_bass_drum_and_snare_as_distinct_percussion_instruments() {
        let xml = convert(
            r#"\score { \new DrumStaff \drummode { \stemUp bd4 sn bd sn | } \layout {} }"#,
            Options::default(),
        )
        .unwrap();

        assert_eq!(xml.matches("<unpitched>").count(), 4);
        assert!(xml.contains("<sign>percussion</sign>"));
        assert!(xml.contains("<midi-channel>10</midi-channel>"));
        assert!(xml.contains("<midi-unpitched>37</midi-unpitched>"));
        assert!(xml.contains("<midi-unpitched>39</midi-unpitched>"));
    }

    #[test]
    fn joins_printable_exercises_in_document_order() {
        let xml = convert(
            r#"\bookpart {
                \markup { First exercise }
                \score { \new DrumStaff \drummode { sn1 | } \layout {} }
                \score { \new Staff { c1 } \midi {} }
                \markup { Second exercise }
                \score { \new DrumStaff \drummode { sn2 sn | } \layout {} }
            }"#,
            Options::default(),
        )
        .unwrap();

        assert!(xml.find("First exercise").unwrap() < xml.find("Second exercise").unwrap());
        assert_eq!(xml.matches("<measure number=").count(), 2);
        assert_eq!(xml.matches("<unpitched>").count(), 3);
        assert!(xml.contains("new-system=\"yes\""));
        assert!(!xml.contains("<pitch>"));
    }
}
