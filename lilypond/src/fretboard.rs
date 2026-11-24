// SPDX-License-Identifier: GPL-3.0-only
use crate::lilypond::{pitch_prefix, read_track};
use crate::model::{Event, Pitch, Time, Track};
use crate::syntax::{Cursor, Node};
use crate::{Result, check};
use std::collections::BTreeMap;

#[derive(Clone, Copy, Debug)]
pub struct Finger {
    pub fret: u8,
    pub number: Option<u8>,
    pub barre: Option<&'static str>,
}

pub type Frame = [Option<Finger>; 6];

fn parse_frame(source: &str) -> Result<Frame> {
    let mut frame = [None; 6];
    let strings = source.trim_end_matches(';').split(';').collect::<Vec<_>>();
    check(strings.len() == frame.len(), "Expected six guitar strings")?;
    for (slot, value) in frame.iter_mut().zip(strings) {
        if value == "x" {
            continue;
        }
        let mut fields = value.split('-');
        let fret = match fields.next().unwrap() {
            "o" => 0,
            value => value
                .parse()
                .map_err(|_| format!("Invalid guitar fret: {value}"))?,
        };
        let number = fields
            .next()
            .map(str::parse)
            .transpose()
            .map_err(|_| "Invalid finger number")?;
        let barre = match fields.next() {
            None => None,
            Some("(") => Some("start"),
            Some(")") => Some("stop"),
            _ => return Err("Invalid guitar barre".into()),
        };
        *slot = Some(Finger {
            fret,
            number,
            barre,
        });
    }
    Ok(frame)
}

fn pc(pitch: Pitch) -> i32 {
    pitch.semitone().rem_euclid(12)
}

type ChordKey<'a> = (i32, &'a str, Option<i32>);

fn key(event: &Event) -> ChordKey<'_> {
    let harmony = event.harmony.as_ref().unwrap();
    (
        pc(event.members[0].pitch),
        &harmony.quality,
        harmony.bass.map(pc),
    )
}

fn definitions<'s>(
    nodes: &[Node<'s>],
    before: usize,
) -> Result<BTreeMap<(i32, String, Option<i32>), Frame>> {
    let mut result = BTreeMap::new();
    let mut cursor = Cursor::new(nodes);
    while let Some(node) = cursor.next().filter(|node| node.start < before) {
        if node.word() != "\\storePredefinedDiagram" {
            continue;
        }
        check(
            cursor.take("fret diagram table")?.scheme() == Some("#default-fret-table"),
            "Unsupported fret diagram table",
        )?;
        let first = cursor.index;
        check(
            cursor.eat("\\chordmode"),
            "Expected a chord in fret diagram definition",
        )?;
        cursor.take("fret diagram chord")?;
        let track = read_track(&cursor.nodes[first..cursor.index], nodes, true)?;
        let [event] = track.events.as_slice() else {
            return Err("Expected one fret diagram chord".into());
        };
        check(event.pitched(), "Expected a pitched fret diagram chord")?;
        check(
            cursor.take("guitar tuning")?.scheme() == Some("#guitar-tuning"),
            "Unsupported fretboard tuning",
        )?;
        let frets = cursor.take("fret positions")?;
        let diagram = frets
            .text()
            .or_else(|| frets.scheme()?.strip_prefix("#\"")?.strip_suffix('"'))
            .ok_or("Expected literal fret positions")?;
        let (root, quality, bass) = key(event);
        result.insert((root, quality.into(), bass), parse_frame(diagram)?);
    }
    Ok(result)
}

fn preset(event: &Event) -> Option<&'static str> {
    let pitch = event.members[0].pitch;
    let (root, quality, bass) = key(event);
    if bass.is_some_and(|bass| bass != root) {
        return None;
    }
    include_str!("guitar-fretboards.tsv")
        .lines()
        .filter_map(|row| {
            let (name, shape) = row.split_once('\t')?;
            let (spelling, kind) = name.split_once(':').unwrap_or((name, ""));
            let (note, "") = pitch_prefix(spelling, "nederlands")? else {
                return None;
            };
            (kind == quality && pc(note) == root)
                .then_some((note.step == pitch.step && note.alter == pitch.alter, shape))
        })
        .max_by_key(|(exact, _)| *exact)
        .map(|(_, shape)| shape)
}

fn inversion(event: &Event) -> Option<Frame> {
    let (root, quality, bass) = key(event);
    let intervals: &[i32] = match quality {
        "" => &[0, 4, 7],
        "m" => &[0, 3, 7],
        "7" => &[0, 4, 7, 10],
        "m7" => &[0, 3, 7, 10],
        "maj7" => &[0, 4, 7, 11],
        _ => return None,
    };
    let tuning = [40, 45, 50, 55, 59, 64];
    let bass = bass.unwrap_or(root);
    let tones = intervals.iter().fold(1_u16 << bass, |mask, offset| {
        mask | (1 << ((root + offset) % 12))
    });
    let choices: [Vec<Option<u8>>; 6] = tuning.map(|open| {
        std::iter::once(None)
            .chain(
                (0..=12)
                    .filter(|fret| tones & (1 << ((open + i32::from(*fret)) % 12)) != 0)
                    .map(Some),
            )
            .collect()
    });
    type Preference = (u8, usize, u16, [Option<u8>; 6]);
    let mut best: Option<Preference> = None;
    let mut frame = [None; 6];

    fn search(
        tuning: &[i32; 6],
        choices: &[Vec<Option<u8>>; 6],
        bass: i32,
        tones: u16,
        frame: &mut [Option<u8>; 6],
        best: &mut Option<Preference>,
        index: usize,
    ) {
        if index < frame.len() {
            for &choice in &choices[index] {
                frame[index] = choice;
                search(tuning, choices, bass, tones, frame, best, index + 1);
            }
            return;
        }
        let mut lowest = i32::MAX;
        let mut played = 0;
        let mut mask = 0;
        let (mut low_fret, mut high_fret) = (u8::MAX, 0);
        let mut sum = 0;
        for (&open, &fret) in tuning.iter().zip(frame.iter()) {
            if let Some(fret) = fret {
                played += 1;
                sum += u16::from(fret);
                let note = open + i32::from(fret);
                lowest = lowest.min(note);
                mask |= 1 << (note % 12);
                if fret > 0 {
                    low_fret = low_fret.min(fret);
                    high_fret = high_fret.max(fret);
                }
            }
        }
        let span = high_fret.saturating_sub(low_fret);
        if mask != tones || lowest % 12 != bass || span > 4 {
            return;
        }
        let rank = (high_fret, 6 - played, sum, *frame);
        if best.as_ref().is_none_or(|best| rank < *best) {
            *best = Some(rank);
        }
    }

    search(&tuning, &choices, bass, tones, &mut frame, &mut best, 0);
    best.map(|entry| {
        entry.3.map(|fret| {
            fret.map(|fret| Finger {
                fret,
                number: None,
                barre: None,
            })
        })
    })
}

pub fn align(
    chords: &Track,
    fretboards: &Track,
    nodes: &[Node<'_>],
    before: usize,
) -> Result<BTreeMap<Time, Frame>> {
    let custom = definitions(nodes, before)?;
    let mut events = chords
        .events
        .iter()
        .filter(|event| event.pitched())
        .peekable();
    let mut frames = BTreeMap::new();
    let mut known = BTreeMap::new();
    for event in fretboards.events.iter().filter(|event| event.pitched()) {
        while events
            .peek()
            .is_some_and(|chord| chord.position < event.position)
        {
            events.next();
        }
        check(
            events
                .peek()
                .is_some_and(|chord| chord.position == event.position && key(chord) == key(event)),
            "Fretboard and chord symbol do not align",
        )?;
        let (root, quality, bass) = key(event);
        let key = (root, quality.to_owned(), bass);
        let frame = if let Some(frame) = custom.get(&key).or_else(|| known.get(&key)) {
            *frame
        } else if let Some(frame) = preset(event) {
            parse_frame(frame)?
        } else {
            inversion(event).ok_or_else(|| {
                format!(
                    "No guitar voicing for MIDI pitch class {root}, chord {quality}, bass {bass:?}"
                )
            })?
        };
        frames.insert(event.position, frame);
        known.insert(key, frame);
    }
    Ok(frames)
}
