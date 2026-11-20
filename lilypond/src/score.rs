use crate::lilypond::{Score, set_layout};
use crate::model::*;
use crate::syntax::{self, Cursor, Kind, Node};
use crate::{Result, check};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Default)]
pub struct Barline {
    pub repeat: Option<(&'static str, Option<usize>)>,
    pub ending: Option<(String, &'static str)>,
    pub final_bar: bool,
}

type Layout = Vec<(&'static str, &'static str)>;

pub struct Plan {
    pub boundaries: Vec<Time>,
    pub divisions: i64,
    pub lyrics: Lyrics,
    pub pickup_number: i64,
    pub breaks: BTreeMap<Time, Layout>,
    pub fixed_systems: BTreeSet<usize>,
    pub rest_counts: BTreeMap<usize, usize>,
    pub left: BTreeMap<Time, Barline>,
    pub right: BTreeMap<Time, Barline>,
}

pub fn plan(score: &mut Score<'_, '_>, page_breaks: &[i64]) -> Result<Plan> {
    let openbook = score.info.title.is_some();
    let melody = &mut score.melody;
    let chords = &score.chords;
    let unequal_length = melody.position != chords.position;
    if openbook {
        if melody.pickup == Time::ZERO {
            melody.pickup = chords.pickup;
        }
        melody.position = melody.position.max(chords.position);
        if melody.repeats.is_empty() {
            melody.repeats = chords.repeats.clone();
        }
    } else {
        check(
            !unequal_length,
            format!(
                "Track lengths differ: {} vs {}",
                melody.position, chords.position
            ),
        )?;
        check(melody.pickup == chords.pickup, "Track pickups differ")?;
        check(melody.repeats == chords.repeats, "Track repeats differ")?;
    }
    let boundaries = measure_boundaries(
        melody,
        openbook && unequal_length && !chords.events.iter().any(Event::pitched),
    )?;
    let on_bar = |p: &Time| boundaries.binary_search(p).is_ok();
    melody.events = split_rests(std::mem::take(&mut melody.events), &boundaries)?;
    melody.secondary = split_rests(std::mem::take(&mut melody.secondary), &boundaries)?;
    resolve_ties(&mut melody.events);
    resolve_ties(&mut melody.secondary);
    check(
        melody.keys.keys().chain(melody.clefs.keys()).all(on_bar),
        "Key or clef change inside a measure",
    )?;
    check(
        openbook
            || melody
                .bar_checks
                .iter()
                .chain(&chords.bar_checks)
                .all(on_bar),
        "Bar check off a measure boundary",
    )?;
    check(
        chords.marks.keys().all(on_bar),
        "Rehearsal mark off a measure boundary",
    )?;
    check(
        chords.navigation.keys().all(on_bar),
        "Navigation mark off a measure boundary",
    )?;
    let mut breaks = BTreeMap::<Time, Layout>::new();
    for track in [&*melody, chords] {
        for (position, attributes) in &track.breaks {
            check(
                on_bar(position),
                format!("Layout break off a measure boundary at {position}"),
            )?;
            let layout = breaks.entry(*position).or_default();
            for &(key, value) in attributes {
                check(
                    !layout.iter().any(|&(k, v)| k == key && v != value),
                    format!("Conflicting break commands at {position}"),
                )?;
                set_layout(layout, (key, value));
            }
        }
    }
    let pickup_number = i64::from(
        melody.pickup == Time::ZERO
            || melody.pickup
                >= melody
                    .meters
                    .get(&Time::ZERO)
                    .copied()
                    .unwrap_or_default()
                    .length(),
    );
    for &number in page_breaks {
        let index = number - pickup_number;
        check(
            index > 0 && (index as usize) < boundaries.len() - 1,
            format!("Page break before invalid measure {number}"),
        )?;
        let layout = breaks.entry(boundaries[index as usize]).or_default();
        set_layout(layout, ("new-page", "yes"));
        set_layout(layout, ("new-system", "yes"));
    }
    let mut rest_counts = BTreeMap::new();
    for (start, end) in &melody.compressed_rests {
        if let (Ok(start), Ok(end)) = (
            boundaries.binary_search(start),
            boundaries.binary_search(end),
        ) {
            rest_counts.insert(start, end - start);
        }
    }
    let mut fixed_systems = BTreeSet::new();
    if openbook {
        let system_starts: Vec<_> = breaks
            .iter()
            .filter(|(p, attributes)| {
                **p > Time::ZERO
                    && **p < chords.position
                    && attributes.contains(&("new-system", "yes"))
            })
            .map(|(p, _)| boundaries.binary_search(p).unwrap())
            .collect();
        if !system_starts.is_empty() {
            let harmony_end = boundaries.partition_point(|p| *p <= chords.position) - 1;
            let starts = std::iter::once(0).chain(system_starts.iter().copied());
            let ends = system_starts
                .iter()
                .copied()
                .chain(std::iter::once(harmony_end));
            for (first, last) in starts.zip(ends) {
                let bars = (last - first) as i64
                    - rest_counts
                        .range(first..last)
                        .map(|(_, &n)| n as i64 - 1)
                        .sum::<i64>();
                if bars <= 12 {
                    fixed_systems.extend(first + 1..last);
                }
            }
        }
    }
    let (mut left, mut right) = repeat_barlines(&melody.repeats, &boundaries)?;
    manual_repeats(&melody.repeat_commands, &boundaries, &mut left, &mut right)?;
    for &p in melody.final_barlines.union(&chords.final_barlines) {
        check(
            p > Time::ZERO && on_bar(&p),
            "Final barline off a measure boundary",
        )?;
        right.entry(p).or_default().final_bar = true;
    }
    let mut divisions = 1_i64;
    for event in melody
        .events
        .iter()
        .chain(&melody.secondary)
        .chain(&chords.events)
    {
        let denominator = (event.length() * Time::integer(4)).denominator;
        divisions = (divisions / gcd(divisions, denominator))
            .checked_mul(denominator)
            .ok_or("MusicXML divisions exceed the supported integer range")?;
    }
    let lyrics = read_lyrics(score)?;
    Ok(Plan {
        boundaries,
        divisions,
        lyrics,
        pickup_number,
        breaks,
        fixed_systems,
        rest_counts,
        left,
        right,
    })
}

fn measure_boundaries(track: &Track, checked_melody: bool) -> Result<Vec<Time>> {
    check(
        !track.events.is_empty() && track.position > Time::ZERO,
        "Empty melody",
    )?;
    check(track.pickup >= Time::ZERO, "Invalid pickup duration")?;
    let mut lengths = BTreeMap::new();
    lengths.extend(
        track
            .meters
            .iter()
            .map(|(&time, meter)| (time, meter.length())),
    );
    lengths.extend(track.measure_lengths.clone());
    let mut current = lengths.get(&Time::ZERO).copied().unwrap_or(Time::ONE);
    let mut boundaries = vec![Time::ZERO];
    let mut checks = if checked_melody {
        track
            .bar_checks
            .iter()
            .copied()
            .filter(|p| *p > Time::ZERO && *p < track.position)
            .collect::<Vec<_>>()
    } else {
        Vec::new()
    };
    checks.sort_unstable();
    for target in lengths
        .keys()
        .copied()
        .filter(|&p| p > Time::ZERO)
        .chain(std::iter::once(track.position))
    {
        while *boundaries.last().unwrap() < target {
            let before = *boundaries.last().unwrap();
            let size = if before == Time::ZERO && track.pickup != Time::ZERO {
                track.pickup
            } else {
                current
            };
            let mut next = (before + size).min(target);
            if !checks.is_empty()
                && track
                    .events
                    .iter()
                    .any(|e| e.pitched() && e.position < next && next < e.position + e.length())
                && let Some(&mark) = checks.get(checks.partition_point(|&p| p <= before))
                && mark < target
            {
                next = mark;
            }
            check(next > before, "Invalid measure length")?;
            boundaries.push(next);
        }
        if let Some(&length) = lengths.get(&target) {
            current = length;
        }
    }
    Ok(boundaries)
}

fn split_rests(events: Vec<Event>, boundaries: &[Time]) -> Result<Vec<Event>> {
    let mut supported = Vec::new();
    for shift in 0..7 {
        for scale in [Time::ONE, Time::new(3, 2), Time::new(7, 4)] {
            supported.push(scale / Time::integer(1 << shift));
        }
    }
    supported.sort_unstable_by(|a, b| b.cmp(a));
    let mut result = Vec::new();
    for event in events {
        if event.pitched() || event.grace.is_some() {
            result.push(event);
            continue;
        }
        let end = event.position + event.length();
        let mut pos = event.position;
        let scale = event.tuplet.unwrap_or(Time::ONE);
        while pos < end {
            let index = boundaries.partition_point(|&p| p <= pos);
            let bar = *boundaries
                .get(index)
                .ok_or("Rest is off the end of the score")?;
            let available = bar.min(end) - pos;
            let complete =
                event.rest == Some('R') && pos == boundaries[index - 1] && available == bar - pos;
            let span = if complete {
                available
            } else {
                supported
                    .iter()
                    .map(|&v| v * scale)
                    .find(|&v| v <= available)
                    .ok_or_else(|| format!("Unsupported rest duration {available}"))?
            };
            let mut item = event.clone();
            item.position = pos;
            item.written = span / scale;
            item.scale = Time::ONE;
            item.measure_rest = complete;
            if pos != event.position {
                item.directions.clear();
                item.markings.clear();
                item.tuplet_start = false;
            }
            pos += span;
            item.tuplet_stop &= pos == end;
            result.push(item);
        }
    }
    Ok(result)
}

fn resolve_ties(events: &mut [Event]) {
    let mut previous: Option<usize> = None;
    for index in 0..=events.len() {
        if events.get(index).is_some_and(|event| event.grace.is_some()) {
            continue;
        }
        if let Some(before) = previous
            && events[before].tied
        {
            let previous = &events[before];
            let end = previous.position + previous.length();
            let pitches: Vec<_> = previous.members.iter().map(|m| m.pitch).collect();
            for pitch in pitches {
                let target = events
                    .get(index)
                    .filter(|e| e.pitched() && e.position == end)
                    .and_then(|e| {
                        e.members
                            .iter()
                            .find(|m| m.pitch == pitch)
                            .or_else(|| {
                                e.members
                                    .iter()
                                    .find(|m| m.pitch.semitone() == pitch.semitone())
                            })
                            .map(|m| m.pitch)
                    });
                if let Some(target) = target {
                    events[before].tie_starts.insert(pitch);
                    events[index].tie_stops.insert(target);
                } else {
                    events[before].open_ties.insert(pitch);
                }
            }
        }
        previous = Some(index);
    }
}

type Barlines = BTreeMap<Time, Barline>;

fn repeat_barlines(repeats: &[Repeat], boundaries: &[Time]) -> Result<(Barlines, Barlines)> {
    let (mut left, mut right) = (Barlines::new(), Barlines::new());
    let mut ordered = Vec::from_iter(repeats);
    ordered.sort_by_key(|repeat| repeat.start);
    for repeat in ordered {
        check(
            std::iter::once(&repeat.start)
                .chain(std::iter::once(&repeat.body_end))
                .chain(repeat.ends.iter().map(|(end, _)| end))
                .all(|p| boundaries.binary_search(p).is_ok()),
            "Repeat or ending is off a measure boundary",
        )?;
        left.entry(repeat.start).or_default().repeat = Some(("forward", None));
        let mut end = repeat.body_end;
        if repeat.ends.is_empty() {
            right.entry(end).or_default().repeat = Some(("backward", Some(repeat.times)));
        }
        for (index, (ending_end, passes)) in repeat.ends.iter().enumerate() {
            let label = passes
                .iter()
                .map(usize::to_string)
                .collect::<Vec<_>>()
                .join(",");
            left.entry(end).or_default().ending = Some((label.clone(), "start"));
            let backward = index + 1 < repeat.ends.len() || repeat.ends.len() == 1;
            let bar = right.entry(*ending_end).or_default();
            bar.ending = Some((label, if backward { "stop" } else { "discontinue" }));
            if backward {
                bar.repeat = Some(("backward", Some(repeat.times)));
            }
            end = *ending_end;
        }
    }
    Ok((left, right))
}

fn manual_repeats(
    commands: &BTreeMap<Time, Vec<String>>,
    boundaries: &[Time],
    left: &mut Barlines,
    right: &mut Barlines,
) -> Result<()> {
    let mut active = None;
    for (&pos, tokens) in commands {
        check(
            boundaries.binary_search(&pos).is_ok(),
            "Repeat command is off a measure boundary",
        )?;
        let mut stream = tokens.iter().map(String::as_str);
        while let Some(cmd) = stream.next() {
            match cmd {
                "start-repeat" => left.entry(pos).or_default().repeat = Some(("forward", None)),
                "end-repeat" => right.entry(pos).or_default().repeat = Some(("backward", Some(2))),
                "volta" => {
                    let value = stream.next().ok_or("Expected volta label")?;
                    if value == "#f" {
                        right.entry(pos).or_default().ending =
                            Some((active.take().ok_or("No volta to close")?, "stop"));
                    } else {
                        check(
                            active.is_none()
                                && value.split(',').all(|n| {
                                    n.trim().trim_end_matches('.').parse::<usize>().is_ok()
                                }),
                            format!("Invalid volta label {value:?}"),
                        )?;
                        let label = value.replace([' ', '.'], "");
                        active = Some(label.clone());
                        left.entry(pos).or_default().ending = Some((label, "start"));
                    }
                }
                _ => return Err(format!("Unsupported repeat command {cmd:?}")),
            }
        }
    }
    check(active.is_none(), "Unclosed volta")
}

fn lyric_nodes<'s>(nodes: &[Node<'s>], out: &mut Vec<Node<'s>>) -> Result<()> {
    let mut cursor = Cursor::new(nodes);
    while let Some(node) = cursor.next() {
        match &node.kind {
            Kind::Group(_, children) => lyric_nodes(children, out)?,
            _ if node.word() == "\\markup" => {
                out.push(node.clone());
                out.extend_from_slice(syntax::markup_argument(&mut cursor)?);
            }
            _ if node.word() == "\\lyricmode" => {}
            _ => out.push(node.clone()),
        }
    }
    Ok(())
}

fn read_lyrics(score: &Score<'_, '_>) -> Result<Lyrics> {
    let mut events: BTreeMap<u8, Vec<&Event>> = BTreeMap::new();
    for event in score.melody.events.iter().chain(&score.melody.secondary) {
        events.entry(event.lyric_voice).or_default().push(event);
    }
    let mut result = Lyrics::default();
    for (section, (voice_name, nodes)) in score.lyric_sections.iter().enumerate() {
        let number = section + 1;
        check(
            score.named_voices.as_slice() == [*voice_name],
            format!("Expected one melody voice named {voice_name:?}"),
        )?;
        let mut lyrics = Vec::new();
        lyric_nodes(nodes, &mut lyrics)?;
        let mut cursor = Cursor::new(&lyrics);
        let mut voice = 0;
        let mut previous: Option<((u8, Time), usize)> = None;
        let mut consumed = None;
        let mut next_voice = None;
        let mut next_event = 0;
        let mut ignore_melismata = false;
        let mut unplaced = Vec::<String>::new();
        while let Some(node) = cursor.next() {
            let word = node.word();
            if matches!(word, "\\set" | "\\unset") {
                let property = cursor.take("lyric property")?.word();
                match property.rsplit('.').next().unwrap_or(property) {
                    "ignoreMelismata" => {
                        ignore_melismata = if word == "\\set" {
                            check(cursor.eat("="), "Expected = for ignoreMelismata")?;
                            matches!(
                                cursor.take("ignoreMelismata value")?.scheme(),
                                Some("#t" | "##t")
                            )
                        } else {
                            false
                        };
                    }
                    "associatedVoice" if word == "\\set" => {
                        check(cursor.eat("="), "Expected = for associatedVoice")?;
                        let name = cursor
                            .take("associated voice")?
                            .text()
                            .ok_or("Expected a voice name for the lyric")?;
                        let v = if name == *voice_name {
                            0
                        } else {
                            name.parse()
                                .map_err(|_| format!("Unknown lyric voice {name:?}"))?
                        };
                        check(
                            events.get(&v).is_some_and(|events| {
                                events.iter().any(|e| e.pitched() && e.grace.is_none())
                            }),
                            format!("Unknown lyric voice {name:?}"),
                        )?;
                        next_voice = Some(v);
                    }
                    _ => return Err(format!("Unsupported lyric set {property:?}")),
                }
                continue;
            }
            if matches!(word, "--" | "__") {
                if !unplaced.is_empty() {
                    unplaced.push(word.into());
                } else {
                    let (key, _) = previous.ok_or("Lyric hyphen or extender without a syllable")?;
                    let syllable = &mut result
                        .syllables
                        .get_mut(&key)
                        .unwrap()
                        .last_mut()
                        .unwrap()
                        .1;
                    if word == "--" {
                        syllable.followed_by_hyphen = true;
                    } else {
                        syllable.extend = true;
                    }
                }
                continue;
            }
            let is_skip = word == "_";
            let mut italic = false;
            let text = if let Some(text) = node.text() {
                syntax::unquote(text)
            } else if word == "\\markup" {
                let markup = syntax::markup_argument(&mut cursor)?;
                italic = syntax::contains(markup, "\\italic");
                syntax::markup_text(markup).trim().into()
            } else {
                check(
                    !word.starts_with('\\') && !word.is_empty(),
                    format!(
                        "Unsupported lyric at source character {}: {}",
                        node.start,
                        node.describe()
                    ),
                )?;
                word.replace('_', " ")
            };
            let current = events.get(&voice).map_or(&[][..], Vec::as_slice);
            let next = (next_event..current.len()).find(|&index| {
                let event = current[index];
                event.pitched()
                    && event.grace.is_none()
                    && (ignore_melismata
                        || (!event.slur_continuation && event.tie_stops.is_empty()))
            });
            let Some(index) = next else {
                if !is_skip && !text.is_empty() {
                    unplaced.push(text);
                }
                continue;
            };
            let event = current[index];
            let event_voice = voice;
            next_event = index + 1;
            consumed = Some((event_voice, index));
            if let Some(target) = next_voice.take() {
                voice = target;
                let start = event.position + event.length();
                next_event = events[&voice].partition_point(|item| item.position < start);
            }
            if is_skip {
                continue;
            }
            let follows_hyphen = previous.is_some_and(|(key, _)| {
                result.syllables[&key].last().unwrap().1.followed_by_hyphen
            });
            let key = (event_voice, event.position);
            result.syllables.entry(key).or_default().push((
                number,
                Syllable {
                    text,
                    follows_hyphen,
                    followed_by_hyphen: false,
                    extend: false,
                    extend_type: None,
                    italic,
                },
            ));
            previous = Some((key, index));
        }
        if let Some((key, previous_index)) = previous {
            let last = &mut result
                .syllables
                .get_mut(&key)
                .unwrap()
                .last_mut()
                .unwrap()
                .1;
            if last.extend {
                let (voice, mut index) = consumed.unwrap();
                let items = &events[&voice];
                while index + 1 < items.len()
                    && !items[index].tie_starts.is_empty()
                    && !items[index + 1].tie_stops.is_empty()
                {
                    index += 1;
                }
                if voice != key.0 || index > previous_index {
                    last.extend_type = Some("start");
                    result
                        .stops
                        .entry((voice, items[index].position))
                        .or_default()
                        .push(number);
                } else {
                    last.extend = false;
                }
            }
        }
        if !unplaced.is_empty() {
            result.unplaced.push((number, unplaced));
        }
    }
    Ok(result)
}
