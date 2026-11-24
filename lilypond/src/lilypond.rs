use crate::model::*;
use crate::syntax::{self, Cursor, Group, Kind, Node};
use crate::{Result, check};
use std::collections::BTreeMap;

#[derive(Clone, Copy)]
struct Duration {
    written: Time,
    scale: Time,
}

impl Default for Duration {
    fn default() -> Self {
        Self {
            written: Time::new(1, 4),
            scale: Time::ONE,
        }
    }
}

fn number(word: &str) -> Result<i64> {
    word.parse()
        .map_err(|_| format!("Expected an integer, found {word:?}"))
}

fn ratio(word: &str) -> Result<Time> {
    if word.contains('/') {
        Time::parse(word).ok_or_else(|| format!("Invalid fraction {word:?}"))
    } else {
        number(word).map(Time::integer)
    }
}

fn parse_duration(word: &str) -> Result<(Option<Duration>, &str)> {
    let end = word.bytes().take_while(u8::is_ascii_digit).count();
    if end == 0 {
        return Ok((None, word));
    }
    let denominator = number(&word[..end])?;
    check(denominator > 0, "Invalid duration")?;
    let mut tail = &word[end..];
    let mut written = Time::new(1, denominator);
    let mut dot = written;
    while let Some(rest) = tail.strip_prefix('.') {
        tail = rest;
        dot = dot / Time::integer(2);
        written += dot;
    }
    let mut scale = Time::ONE;
    while let Some(rest) = tail.strip_prefix('*') {
        let mut size = rest.bytes().take_while(u8::is_ascii_digit).count();
        if let Some(denominator) = rest[size..].strip_prefix('/') {
            let digits = denominator.bytes().take_while(u8::is_ascii_digit).count();
            if digits > 0 {
                size += 1 + digits;
            }
        }
        if size == 0 {
            break;
        }
        scale = scale * ratio(&rest[..size])?;
        tail = &rest[size..];
    }
    Ok((Some(Duration { written, scale }), tail))
}

fn note_name(language: &str, name: &str) -> Option<(usize, i8)> {
    let western = ["c", "d", "e", "f", "g", "a", "b"];
    let german = ["c", "d", "e", "f", "g", "a", "h"];
    let solfege = ["do", "re", "mi", "fa", "sol", "la", "si"];
    let (steps, alterations) = match language {
        "nederlands" => {
            match name {
                "as" => return Some((5, -1)),
                "es" => return Some((2, -1)),
                "eses" => return Some((2, -2)),
                _ => {}
            }
            (western, ["eses", "es", "is", "isis"])
        }
        "english" => (western, ["ff", "f", "s", "ss"]),
        "deutsch" | "norsk" | "suomi" => {
            match name {
                "b" | "hes" => return Some((6, -1)),
                "as" => return Some((5, -1)),
                "ases" | "asas" => return Some((5, -2)),
                "es" => return Some((2, -1)),
                _ => {}
            }
            (german, ["eses", "es", "is", "isis"])
        }
        "svenska" => {
            match name {
                "b" | "hess" => return Some((6, -1)),
                "as" => return Some((5, -1)),
                "es" => return Some((2, -1)),
                _ => {}
            }
            (german, ["essess", "ess", "iss", "ississ"])
        }
        "italiano" | "catalan" => (solfege, ["bb", "b", "d", "dd"]),
        "espanol" | "portugues" => (solfege, ["bb", "b", "s", "ss"]),
        "vlaams" => (solfege, ["bb", "b", "k", "kk"]),
        _ => return None,
    };
    for (step, base) in steps.iter().enumerate() {
        if let Some(suffix) = name.strip_prefix(base) {
            if suffix.is_empty() {
                return Some((step, 0));
            }
            if let Some(index) = alterations.iter().position(|&a| a == suffix) {
                return Some((step, [-2, -1, 1, 2][index]));
            }
        }
    }
    None
}

pub(crate) fn pitch_prefix<'s>(word: &'s str, lang: &str) -> Option<(Pitch, &'s str)> {
    let name_end = word.bytes().take_while(u8::is_ascii_alphabetic).count();
    let (step, alter) = note_name(lang, &word[..name_end])?;
    let mut octave = 3_i32;
    let mut end = name_end;
    for b in word[end..].bytes() {
        match b {
            b'\'' => octave = octave.checked_add(1)?,
            b',' => octave = octave.checked_sub(1)?,
            b'!' | b'?' => {}
            _ => break,
        }
        end += 1;
    }
    Some((
        Pitch {
            step,
            alter,
            octave,
        },
        &word[end..],
    ))
}

fn relative(mut pitch: Pitch, last: Pitch) -> Pitch {
    pitch.octave += last.octave - 3 - ((pitch.step as i32 - last.step as i32 + 3).div_euclid(7));
    pitch
}

#[derive(Clone, Copy, Default)]
struct Scope {
    tuplet: Option<Time>,
    grace: Option<bool>,
}

#[derive(Clone)]
struct Reader<'s> {
    track: Track,
    chords: bool,
    drums: bool,
    breath_mark: &'static str,
    language: &'s str,
    duration: Duration,
    relative_to: Option<Pitch>,
    lyric_voice: u8,
    stem: Option<&'static str>,
    next_chord_parenthesis: Option<&'static str>,
    alternate_chords_open: bool,
    omit_next_bracket: bool,
    hide_time: bool,
    numeric_time: bool,
    notehead: &'static str,
    next_notehead: Option<&'static str>,
    next_parenthesized: bool,
    improvising: bool,
    glissando: Option<&'static str>,
    next_glissando: &'static str,
    slurs: Vec<(bool, usize)>,
    beam_start: Option<usize>,
}

impl<'s> Reader<'s> {
    fn new(chords: bool, language: &'s str) -> Self {
        Self {
            track: Track::default(),
            chords,
            drums: false,
            breath_mark: "breath-mark",
            language,
            duration: Duration::default(),
            relative_to: None,
            lyric_voice: 0,
            stem: None,
            next_chord_parenthesis: None,
            alternate_chords_open: false,
            omit_next_bracket: false,
            hide_time: false,
            numeric_time: false,
            notehead: "normal",
            next_notehead: None,
            next_parenthesized: false,
            improvising: false,
            glissando: None,
            next_glissando: "solid",
            slurs: Vec::new(),
            beam_start: None,
        }
    }

    fn resolve(&mut self, mut pitch: Pitch) -> Pitch {
        if let Some(last) = self.relative_to {
            pitch = relative(pitch, last);
            self.relative_to = Some(pitch);
        }
        pitch
    }

    fn parse_pitch(&self, word: &str) -> Result<Pitch> {
        match pitch_prefix(word, self.language) {
            Some((pitch, "")) => Ok(pitch),
            _ => Err(format!("Expected a pitch, found {word:?}")),
        }
    }

    fn exact_duration(&self, word: &str) -> Result<Duration> {
        let (duration, rest) = parse_duration(word)?;
        check(rest.is_empty(), format!("Invalid duration {word:?}"))?;
        duration.ok_or_else(|| format!("Expected a duration, found {word:?}"))
    }

    fn event(
        &mut self,
        scope: Scope,
        members: Vec<Member>,
        rest: Option<char>,
        harmony: Option<Harmony>,
    ) -> Result<()> {
        check(
            self.chords || self.duration.scale == Time::ONE || rest.is_some(),
            "Scaled pitched melody notes are outside this experiment",
        )?;
        check(
            self.next_chord_parenthesis.is_none() || rest.is_none(),
            "Chord parenthesis without a chord",
        )?;
        let parentheses = self.next_chord_parenthesis.take();
        let alternate = self.alternate_chords_open || parentheses == Some("both");
        if parentheses == Some("right") {
            self.alternate_chords_open = false;
        }
        let mut event = Event {
            position: self.track.position,
            written: self.duration.written,
            scale: self.duration.scale,
            members,
            rest,
            harmony,
            tuplet: scope.tuplet,
            grace: scope.grace,
            stem: self.stem,
            lyric_voice: self.lyric_voice,
            chord_parenthesis: parentheses,
            alternate_chord: alternate,
            notehead: "normal",
            ..Event::default()
        };
        if event.pitched() && !self.chords {
            event.slur_continuation = self.slurs.iter().any(|&(phrasing, _)| !phrasing);
            event.notehead = self.next_notehead.take().unwrap_or(if self.improvising {
                "slash"
            } else {
                self.notehead
            });
            event.parenthesized = std::mem::take(&mut self.next_parenthesized);
            event.glissando_stop = self.glissando.take();
        }
        check(
            event.written > Time::ZERO && event.scale > Time::ZERO,
            "Missing duration",
        )?;
        self.track.position += event.length();
        self.track.events.push(event);
        Ok(())
    }

    fn read_duration<'a>(&mut self, part: &'a str, cursor: &mut Cursor<'_, 'a>) -> Result<&'a str> {
        let (mut duration, mut tail) = parse_duration(part)?;
        if duration.is_none()
            && tail.is_empty()
            && cursor.peek_word().starts_with(|c: char| c.is_ascii_digit())
        {
            let (next, suffix) = parse_duration(cursor.peek_word())?;
            if next.is_some() {
                cursor.next();
                duration = next;
                tail = suffix;
            }
        }
        if tail.is_empty() && matches!(cursor.peek_word(), "\\breve" | "\\longa" | "\\maxima") {
            let n = match cursor.next().unwrap().word() {
                "\\breve" => 2,
                "\\longa" => 4,
                _ => 8,
            };
            duration = Some(Duration {
                written: Time::integer(n),
                scale: Time::ONE,
            });
        }
        if let Some(mut duration) = duration {
            while tail.is_empty() && cursor.peek_word().starts_with('*') {
                duration.scale = duration.scale * ratio(&cursor.next().unwrap().word()[1..])?;
            }
            self.duration = duration;
        }
        Ok(tail)
    }

    fn harmony(&self, text: &str) -> Result<Harmony> {
        let (quality, bass) = text
            .split_once('/')
            .map_or((text, None), |(a, b)| (a, Some(b)));
        check(
            quality.is_empty() || quality.starts_with(':'),
            format!("Unsupported chord specifier {text:?}"),
        )?;
        let bass = bass
            .map(|n| self.parse_pitch(n.strip_prefix('+').unwrap_or(n)))
            .transpose()?;
        Ok(Harmony {
            quality: quality.trim_start_matches(':').into(),
            bass,
            spec: text.into(),
        })
    }

    fn read_pitch<'a>(
        &mut self,
        word: &'a str,
        cursor: &mut Cursor<'_, 'a>,
    ) -> Option<(Pitch, &'a str)> {
        let (mut pitch, rest) = pitch_prefix(word, self.language)?;
        if !rest.is_empty() || !cursor.eat("=") {
            return Some((self.resolve(pitch), rest));
        }
        let value = cursor.peek_word();
        let end = value
            .bytes()
            .take_while(|b| matches!(b, b',' | b'\''))
            .count();
        let checked_octave = 3 + value[..end]
            .bytes()
            .map(|b| if b == b'\'' { 1 } else { -1 })
            .sum::<i32>();
        let tail = if end == 0 {
            ""
        } else {
            &cursor.next().unwrap().word()[end..]
        };
        if self.relative_to.is_some() {
            pitch.octave = checked_octave;
            self.relative_to = Some(pitch);
        }
        Some((pitch, tail))
    }

    fn note_pitch<'a>(
        &mut self,
        word: &'a str,
        cursor: &mut Cursor<'_, 'a>,
    ) -> Option<(Pitch, Option<Drum>, &'a str)> {
        if !self.drums {
            return self
                .read_pitch(word, cursor)
                .map(|(pitch, rest)| (pitch, None, rest));
        }
        let end = word.bytes().take_while(u8::is_ascii_alphabetic).count();
        // Default LilyPond drums-style places bassdrum three staff steps below
        // the middle line and snare one step above it.
        let (midi_key, name, step, octave, notehead) = match &word[..end] {
            "bd" | "bassdrum" => (36, "Bass Drum", 3, 4, "normal"),
            "sn" | "snare" => (38, "Acoustic Snare", 0, 5, "normal"),
            "hh" | "hihat" => (42, "Hi-Hat", 2, 5, "x"),
            _ => return None,
        };
        Some((
            Pitch {
                step,
                alter: 0,
                octave,
            },
            Some(Drum {
                midi_key,
                name,
                notehead,
            }),
            &word[end..],
        ))
    }

    fn durable(
        &mut self,
        word: &'s str,
        cursor: &mut Cursor<'_, 's>,
        scope: Scope,
    ) -> Result<bool> {
        let parsed = if let Some((pitch, drum, rest)) = self.note_pitch(word, cursor) {
            Some((Some((pitch, drum)), None, rest))
        } else if let Some(ch @ ('r' | 'R' | 's')) = word.chars().next() {
            (!word[1..].starts_with(|c: char| c.is_ascii_alphabetic())).then_some((
                None,
                Some(ch),
                &word[1..],
            ))
        } else {
            None
        };
        let Some((pitch, rest, tail)) = parsed else {
            return Ok(false);
        };
        let tail = self.read_duration(tail, cursor)?;
        let members = pitch
            .into_iter()
            .map(|(pitch, drum)| Member {
                pitch,
                drum,
                parentheses: false,
                harmonic: false,
            })
            .collect();
        if self.chords && rest.is_none() {
            let tail = if tail.is_empty() && cursor.peek_word().starts_with([':', '/']) {
                cursor.next().unwrap().word()
            } else {
                tail
            };
            let harmony = self.harmony(tail)?;
            self.event(scope, members, rest, Some(harmony))?;
        } else {
            self.event(scope, members, rest, None)?;
            self.suffix(tail, cursor)?;
        }
        Ok(true)
    }

    fn melody_chord(
        &mut self,
        nodes: &[Node<'s>],
        cursor: &mut Cursor<'_, 's>,
        scope: Scope,
    ) -> Result<()> {
        check(!self.chords, "Unsupported chord member")?;
        let mut members: Vec<Member> = Vec::new();
        let mut parentheses = false;
        let mut chord = Cursor::new(nodes);
        while let Some(node) = chord.next() {
            match node.word() {
                "\\parenthesize" => parentheses = true,
                "\\harmonic" => {
                    members
                        .last_mut()
                        .ok_or("Harmonic without a chord member")?
                        .harmonic = true
                }
                word => {
                    let (pitch, drum, "") = self
                        .note_pitch(word, &mut chord)
                        .ok_or_else(|| format!("Expected a chord pitch, found {word:?}"))?
                    else {
                        return Err(format!("Unsupported chord member {word:?}"));
                    };
                    members.push(Member {
                        pitch,
                        drum,
                        parentheses,
                        harmonic: false,
                    });
                    parentheses = false;
                }
            }
        }
        check(!members.is_empty(), "Empty chord")?;
        if self.relative_to.is_some() {
            self.relative_to = Some(members[0].pitch);
        }
        let suffix = self.read_duration("", cursor)?;
        self.event(scope, members, None, None)?;
        self.suffix(suffix, cursor)
    }

    fn last_event(&mut self) -> Result<&mut Event> {
        self.track
            .events
            .last_mut()
            .ok_or_else(|| "Note marking without a note".into())
    }

    fn slur(&mut self, word: &str, placement: Option<&'static str>) -> Result<()> {
        let phrasing = word.starts_with('\\');
        let (kind, number) = if word.ends_with('(') {
            let number = (1..)
                .find(|n| !self.slurs.iter().any(|s| s.1 == *n))
                .unwrap();
            self.slurs.push((phrasing, number));
            ("start", number)
        } else {
            let at = self
                .slurs
                .iter()
                .rposition(|s| s.0 == phrasing)
                .ok_or("Slur end without a start")?;
            ("stop", self.slurs.remove(at).1)
        };
        self.last_event()?.slurs.push((kind, number, placement));
        Ok(())
    }

    fn mark(&mut self, word: &str, placement: Option<&'static str>) -> Result<()> {
        let (group, value) = match word {
            "(" | ")" | "\\(" | "\\)" => return self.slur(word, placement),
            "." | "\\staccato" => ("articulations", "staccato"),
            "-" | "\\tenuto" => ("articulations", "tenuto"),
            ">" | "\\accent" => ("articulations", "accent"),
            "^" | "\\marcato" => ("articulations", "strong-accent"),
            "_" => ("articulations", "detached-legato"),
            "\\trill" => ("ornaments", "trill-mark"),
            "\\prall" => ("ornaments", "inverted-mordent"),
            "\\mordent" => ("ornaments", "mordent"),
            "\\fermata" => ("", "fermata"),
            _ => {
                if let Some((kind, value)) = dynamic(word) {
                    self.last_event()?.directions.push(Direction::new(
                        kind,
                        placement.or(Some("below")),
                        value,
                    ));
                    return Ok(());
                }
                check(
                    !word.is_empty() && word.bytes().all(|c| c.is_ascii_digit()),
                    format!("Unsupported note marking {word:?}"),
                )?;
                ("technical", word)
            }
        };
        self.last_event()?.markings.push(Mark {
            group,
            value: value.into(),
            placement,
        });
        Ok(())
    }

    fn postfix(&mut self, prefix: char, cursor: &mut Cursor<'_, 's>) -> Result<()> {
        let placement = placement(prefix);
        if syntax::is_markup(cursor.peek_word()) {
            cursor.next();
            let markup = syntax::markup_argument(cursor)?;
            self.last_event()?.directions.extend(markup_directions(
                markup,
                placement.or(Some("above")),
                false,
            ));
        } else {
            let node = cursor.take("note marking")?;
            if node.text().is_some() {
                self.last_event()?.directions.extend(markup_directions(
                    std::slice::from_ref(node),
                    placement.or(Some("above")),
                    false,
                ));
            } else {
                self.mark(node.word(), placement)?;
            }
        }
        Ok(())
    }

    fn suffix(&mut self, mut tail: &str, cursor: &mut Cursor<'_, 's>) -> Result<()> {
        while let Some(prefix) = tail.chars().next() {
            tail = &tail[prefix.len_utf8()..];
            match prefix {
                '-' | '^' | '_' if tail.is_empty() => self.postfix(prefix, cursor)?,
                '-' | '^' | '_' => {
                    let size = if tail.starts_with(|c: char| c.is_ascii_digit()) {
                        tail.bytes().take_while(u8::is_ascii_digit).count()
                    } else {
                        tail.chars().next().unwrap().len_utf8()
                    };
                    self.mark(&tail[..size], placement(prefix))?;
                    tail = &tail[size..];
                }
                ':' => {
                    let size = tail.bytes().take_while(u8::is_ascii_digit).count();
                    let n = number(&tail[..size])?;
                    check(n >= 8 && n & (n - 1) == 0, "Invalid tremolo duration")?;
                    self.last_event()?.markings.push(Mark {
                        group: "tremolo",
                        value: (n.ilog2() - 2).to_string(),
                        placement: None,
                    });
                    tail = &tail[size..];
                }
                _ => return Err(format!("Unsupported note suffix {prefix}{tail}")),
            }
        }
        Ok(())
    }

    fn seq(&mut self, nodes: &[Node<'s>], scope: Scope) -> Result<()> {
        let mut cursor = Cursor::new(nodes);
        while cursor.peek().is_some() {
            self.one(&mut cursor, scope)?;
        }
        Ok(())
    }

    fn one(&mut self, cursor: &mut Cursor<'_, 's>, scope: Scope) -> Result<()> {
        let node = cursor.take("music expression")?;
        match &node.kind {
            Kind::Group(Group::Sequence, nodes) => return self.seq(nodes, scope),
            Kind::Group(Group::Simultaneous, nodes) if !self.chords => {
                return self.simultaneous(nodes, scope);
            }
            Kind::Group(Group::Chord, nodes) => return self.melody_chord(nodes, cursor, scope),
            _ => {}
        }
        let word = node.word();
        if self.durable(word, cursor, scope)? {
            return Ok(());
        }
        match word {
            "\\skip" => {
                let tail = self.read_duration("", cursor)?;
                self.event(scope, Vec::new(), Some('s'), None)?;
                self.suffix(tail, cursor)?;
            }
            "\\relative" => {
                let previous = self.relative_to;
                let reference = relative_reference(cursor, self.language);
                self.relative_to = relative_expression(cursor).and_then(|node| {
                    (matches!(node.kind, Kind::Group(_, _))
                        || pitch_prefix(node.word(), self.language).is_some())
                    .then_some(reference)
                });
                self.one(cursor, scope)?;
                self.relative_to = previous;
            }
            "\\absolute" => self.one(cursor, scope)?,
            "\\fixed" => {
                check(cursor.eat("c"), "Only fixed c is supported")?;
                self.one(cursor, scope)?;
            }
            "\\transpose" => {
                let from = self.parse_pitch(cursor.take("transposition origin")?.word())?;
                let to = self.parse_pitch(cursor.take("transposition destination")?.word())?;
                check(from == to, "Only identity transposition is supported")?;
                self.one(cursor, scope)?;
            }
            "\\new" | "\\context" => {
                context_head(cursor)?;
                self.one(cursor, scope)?;
            }
            "\\chordmode" | "\\notemode" => self.one(cursor, scope)?,
            "\\drummode" | "\\drums" if !self.chords => {
                let previous = std::mem::replace(&mut self.drums, true);
                self.one(cursor, scope)?;
                self.drums = previous;
            }
            "\\with" => {
                cursor.take("context settings")?;
            }
            "\\tuplet" | "\\times" => self.tuplet(word, cursor, scope)?,
            "\\grace" | "\\acciaccatura" | "\\appoggiatura" | "\\slashedGrace" | "\\myGrace" => {
                check(scope.grace.is_none(), "Nested grace group")?;
                let first = self.track.events.len();
                self.one(
                    cursor,
                    Scope {
                        grace: Some(matches!(word, "\\acciaccatura" | "\\slashedGrace")),
                        tuplet: None,
                    },
                )?;
                check(
                    !self.chords
                        || self.track.events[first..]
                            .iter()
                            .all(|e| e.rest == Some('s')),
                    "A chord symbol cannot be attached to a grace note",
                )?;
            }
            "\\repeat" => self.repeat(cursor, scope)?,
            "|" => self.track.bar_checks.push(self.track.position),
            "~" => {
                let e = self.last_event()?;
                check(e.pitched(), "Tie without a note")?;
                e.tied = true;
            }
            "[" | "]" if !self.chords => self.beam(word)?,
            "(" | ")" | "\\(" | "\\)" if !self.chords => self.slur(word, None)?,
            word if !self.chords && word.starts_with(['-', '^', '_', ':']) => {
                self.suffix(word, cursor)?
            }
            "\\partial" => {
                check(
                    self.track.position == Time::ZERO,
                    "Expected the pickup before the first event",
                )?;
                check(
                    self.read_duration("", cursor)?.is_empty(),
                    "Invalid pickup duration",
                )?;
                self.track.pickup = self.duration.written * self.duration.scale;
            }
            "\\time" => {
                let signature = cursor.take("time signature")?.word();
                let (beats, beat_type) =
                    signature.split_once('/').ok_or("Invalid time signature")?;
                let (beats, beat_type) = (number(beats)?, number(beat_type)?);
                check(beats > 0 && beat_type > 0, "Invalid time signature")?;
                self.track.meters.insert(
                    self.track.position,
                    Meter {
                        beats,
                        beat_type,
                        numeric: self.numeric_time,
                        hidden: std::mem::take(&mut self.hide_time),
                    },
                );
            }
            "\\key" => {
                let pitch = self.parse_pitch(cursor.take("key pitch")?.word())?;
                let mode = match cursor.take("key mode")?.word() {
                    "\\major" => "major",
                    "\\minor" => "minor",
                    word => return Err(format!("Unsupported key mode {word}")),
                };
                let fifths = [0, 2, 4, -1, 1, 3, 5][pitch.step] + 7 * i32::from(pitch.alter)
                    - if mode == "minor" { 3 } else { 0 };
                self.track.keys.insert(self.track.position, (fifths, mode));
            }
            "\\clef" if !self.chords => self.clef(cursor)?,
            "\\tempo" => self.tempo(cursor)?,
            "\\mark" | "\\textMark" => {
                let markup = if syntax::is_markup(cursor.peek_word()) {
                    cursor.next();
                    syntax::markup_argument(cursor)?
                } else {
                    let node = cursor.take("rehearsal mark label")?;
                    check(node.text().is_some(), "Unsupported rehearsal mark label")?;
                    std::slice::from_ref(node)
                };
                self.track
                    .directions
                    .entry(self.track.position)
                    .or_default()
                    .extend(markup_directions(markup, Some("above"), word == "\\mark"));
            }
            "\\myWordMark" => {
                let label = cursor
                    .take("section label")?
                    .text()
                    .ok_or("Expected a string after myWordMark")?;
                self.track
                    .directions
                    .entry(self.track.position)
                    .or_default()
                    .push(Direction::new(
                        "rehearsal",
                        Some("above"),
                        syntax::unquote(label),
                    ));
            }
            "\\myMark" => {
                let label = cursor
                    .take("rehearsal mark label")?
                    .text()
                    .ok_or("Expected a string after myMark")?;
                self.track
                    .marks
                    .insert(self.track.position, syntax::unquote(label));
            }
            "\\startRepeat" | "\\endRepeat" => {
                let command = if word == "\\startRepeat" {
                    "start-repeat"
                } else {
                    "end-repeat"
                };
                self.track
                    .repeat_commands
                    .entry(self.track.position)
                    .or_default()
                    .push(command.into());
            }
            "\\endBar" | "\\endTune" => {
                self.track.final_barlines.insert(self.track.position);
            }
            "\\mySegno" | "\\myCoda" if self.chords => {
                self.track
                    .navigation
                    .entry(self.track.position)
                    .or_default()
                    .push(if word == "\\mySegno" { "segno" } else { "coda" });
            }
            "\\startChords" if self.chords => self.track.chord_changes = true,
            "\\LPC" | "\\RPC" | "\\OPC" if self.chords => {
                check(
                    self.next_chord_parenthesis.is_none()
                        && self.alternate_chords_open == (word == "\\RPC"),
                    "Unexpected chord parenthesis",
                )?;
                self.next_chord_parenthesis = Some(match word {
                    "\\LPC" => "left",
                    "\\RPC" => "right",
                    _ => "both",
                });
                if word == "\\LPC" {
                    self.alternate_chords_open = true;
                }
            }
            "\\numericTimeSignature" if !self.chords => self.numeric_time = true,
            "\\voiceOne" | "\\voiceTwo" | "\\voiceThree" | "\\oneVoice" | "\\stemUp"
            | "\\stemDown" | "\\stemNeutral"
                if !self.chords =>
            {
                self.stem = match word {
                    "\\voiceOne" | "\\voiceThree" | "\\stemUp" => Some("up"),
                    "\\voiceTwo" | "\\stemDown" => Some("down"),
                    _ => None,
                };
            }
            "\\xNote" if !self.chords => self.next_notehead = Some("x"),
            "\\parenthesize" if !self.chords => self.next_parenthesized = true,
            "\\improvisationOn" | "\\improvisationOff" if !self.chords => {
                self.improvising = word == "\\improvisationOn"
            }
            "\\repeatTie" | "\\laissezVibrer" | "\\glissando" if !self.chords => {
                let style = self.next_glissando;
                let event = self.last_event()?;
                check(event.pitched(), "Pitch marking without a note")?;
                match word {
                    "\\repeatTie" => event.repeat_tie = true,
                    "\\laissezVibrer" => event.let_ring = true,
                    _ => {
                        event.glissando_start = Some(style);
                        self.glissando = Some(style);
                        self.next_glissando = "solid";
                    }
                }
            }
            "\\arpeggio" if !self.chords => {
                let event = self.last_event()?;
                check(event.members.len() > 1, "Arpeggio without a chord")?;
                let mark = Mark {
                    group: "",
                    value: "arpeggiate".into(),
                    placement: None,
                };
                if !event.markings.contains(&mark) {
                    event.markings.push(mark);
                }
            }
            "\\transposition" if !self.chords => {
                let p = self.parse_pitch(cursor.take("transposition pitch")?.word())?;
                self.track.transposition =
                    Some((p.step, p.semitone() - 12 * p.octave, p.octave - 3));
            }
            "\\compressMMRests" if !self.chords => {
                let first = self.track.events.len();
                self.one(cursor, scope)?;
                for event in &self.track.events[first..] {
                    if event.rest == Some('R') && event.scale > Time::ONE {
                        self.track
                            .compressed_rests
                            .insert(event.position, event.position + event.length());
                    }
                }
            }
            "\\once" | "\\override" | "\\revert" if !self.chords => {
                self.override_command(word, cursor)?
            }
            "\\set" if !self.chords => self.property(cursor)?,
            "\\breathe" if !self.chords => {
                let value = self.breath_mark;
                self.last_event()?.markings.push(Mark {
                    group: "articulations",
                    value: value.into(),
                    placement: None,
                });
            }
            "\\segno" if !self.chords => {
                self.last_event()?
                    .directions
                    .push(Direction::new("segno", Some("above"), ""))
            }
            "\\sustainOn" | "\\sustainOff" if !self.chords => {
                let kind = if word == "\\sustainOn" {
                    "start"
                } else {
                    "stop"
                };
                self.last_event()?
                    .directions
                    .push(Direction::new("pedal", Some("below"), kind));
            }
            "\\ottava" | "\\bendAfter" if !self.chords => self.pitch_mark(word, cursor)?,
            "\\octaveCheck" if self.relative_to.is_some() => {
                self.relative_to =
                    Some(self.parse_pitch(cursor.take("octave check pitch")?.word())?);
            }
            "\\language" => {
                let language = cursor
                    .take("pitch language")?
                    .text()
                    .ok_or("Expected a pitch language")?;
                if known_language(language) {
                    self.language = language;
                }
            }
            "\\break"
            | "\\myBreak"
            | "\\myEndLine"
            | "\\myEndLineVolta"
            | "\\myEndLineVoltaLast"
            | "\\noBreak"
            | "\\pageBreak"
            | "\\noPageBreak" => {
                let command = match word {
                    "\\noBreak" => ("new-system", "no"),
                    "\\pageBreak" => ("new-page", "yes"),
                    "\\noPageBreak" => ("new-page", "no"),
                    _ => ("new-system", "yes"),
                };
                set_layout(
                    self.track.breaks.entry(self.track.position).or_default(),
                    command,
                );
            }
            "\\startChords"
            | "\\endChords"
            | "\\startTune"
            | "\\startSong"
            | "\\endSong"
            | "\\startPart"
            | "\\endPart"
            | "\\myEndLineVoltaNotLast"
            | "\\myFakeEndLine" => {}
            word if !self.chords
                && (dynamic(word).is_some()
                    || matches!(
                        word,
                        "\\staccato"
                            | "\\tenuto"
                            | "\\accent"
                            | "\\marcato"
                            | "\\trill"
                            | "\\prall"
                            | "\\mordent"
                            | "\\fermata"
                    )) =>
            {
                self.mark(word, None)?
            }
            _ => {
                return Err(format!(
                    "Unsupported music at source character {}: {}",
                    node.start,
                    node.describe()
                ));
            }
        }
        Ok(())
    }

    fn clef(&mut self, cursor: &mut Cursor<'_, 's>) -> Result<()> {
        let node = cursor.take("clef")?;
        let clef = node.text().unwrap_or(node.word());
        let split = clef.find(['_', '^']).unwrap_or(clef.len());
        let (sign, line) = match &clef[..split] {
            "treble" => ("G", 2),
            "bass" => ("F", 4),
            "alto" => ("C", 3),
            "tenor" => ("C", 4),
            _ => return Err(format!("Unsupported clef {clef:?}")),
        };
        let shift = match &clef[split..] {
            "" | "_1" | "^1" | "_85" | "^85" => 0,
            "_8" => -1,
            "^8" => 1,
            "_15" => -2,
            "^15" => 2,
            _ => return Err(format!("Unsupported clef {clef:?}")),
        };
        self.track
            .clefs
            .insert(self.track.position, (sign, line, shift));
        Ok(())
    }

    fn pitch_mark(&mut self, word: &str, cursor: &mut Cursor<'_, 's>) -> Result<()> {
        let value = cursor
            .take("literal pitch marking")?
            .scheme()
            .ok_or("Expected literal pitch marking")?
            .trim_start_matches('#');
        if word == "\\ottava" {
            let (shift, at) = match value {
                "1" => ("down", "above"),
                "-1" => ("up", "below"),
                "0" => ("stop", "above"),
                _ => return Err(format!("Unsupported ottava {value:?}")),
            };
            self.track
                .directions
                .entry(self.track.position)
                .or_default()
                .push(Direction::new("octave-shift", Some(at), shift));
        } else {
            check(
                value.parse::<f64>().is_ok_and(f64::is_finite),
                "Expected bend interval",
            )?;
            let event = self.last_event()?;
            check(event.pitched(), "Bend without a pitch")?;
            event.markings.push(Mark {
                group: "bend",
                value: value.into(),
                placement: None,
            });
        }
        Ok(())
    }

    fn tuplet(&mut self, word: &str, cursor: &mut Cursor<'_, 's>, scope: Scope) -> Result<()> {
        check(scope.tuplet.is_none(), "Unsupported or nested tuplet")?;
        let raw = ratio(cursor.take("tuplet ratio")?.word())?;
        check(raw > Time::ZERO, "Invalid tuplet ratio")?;
        let scale = if word == "\\tuplet" {
            Time::ONE / raw
        } else {
            raw
        };
        let group = if cursor.peek_word().starts_with(|c: char| c.is_ascii_digit()) {
            let d = self.exact_duration(cursor.next().unwrap().word())?;
            // python-ly remembers this duration for the next unqualified note.
            self.duration = d;
            Some(d.written * d.scale)
        } else {
            None
        };
        let start = self.track.events.len();
        let at = self.track.position;
        let bracket = !std::mem::take(&mut self.omit_next_bracket);
        self.one(
            cursor,
            Scope {
                tuplet: Some(scale),
                ..scope
            },
        )?;
        check(self.track.events.len() > start, "Empty tuplet")?;
        if !self.chords {
            let size = group.unwrap_or(self.track.position - at);
            let events = &mut self.track.events;
            let mut first = start;
            let mut elapsed = Time::ZERO;
            for index in start..events.len() {
                elapsed += events[index].length();
                if elapsed >= size || index + 1 == events.len() {
                    events[first].tuplet_start = true;
                    events[first].tuplet_bracket = bracket;
                    events[index].tuplet_stop = true;
                    first = index + 1;
                    elapsed = Time::ZERO;
                }
            }
        }
        Ok(())
    }

    fn tempo(&mut self, cursor: &mut Cursor<'_, 's>) -> Result<()> {
        let words = cursor.peek().and_then(Node::text).map(syntax::unquote);
        let is_scheme = cursor.peek().and_then(Node::scheme).is_some();
        let has_text = words.is_some() || is_scheme || syntax::is_markup(cursor.peek_word());
        if words.is_some() || is_scheme {
            cursor.next();
        } else if syntax::is_markup(cursor.peek_word()) {
            cursor.next();
            syntax::markup_argument(cursor)?;
        }
        let (beat, bpm) = if cursor.peek_word().starts_with(|c: char| c.is_ascii_digit()) {
            check(
                self.read_duration("", cursor)?.is_empty(),
                "Invalid tempo duration",
            )?;
            let duration = self.duration;
            check(cursor.eat("="), "Expected = in tempo")?;
            let value = cursor.take("tempo value")?.word();
            let (low, high) = value
                .split_once('-')
                .map_or((value, None), |(low, high)| (low, Some(high)));
            let bpm = (
                number(low)?,
                high.filter(|s| !s.is_empty()).map(number).transpose()?,
            );
            if high == Some("") || cursor.eat("-") {
                cursor.take("upper tempo value")?;
            }
            (duration.written * duration.scale, Some(bpm))
        } else {
            (Time::ZERO, None)
        };
        self.track.tempos.insert(
            self.track.position,
            Tempo {
                words,
                beat,
                bpm,
                has_text,
            },
        );
        Ok(())
    }

    fn property(&mut self, cursor: &mut Cursor<'_, 's>) -> Result<()> {
        let field = cursor.take("property name")?.word();
        let property = field.rsplit('.').next().unwrap_or(field);
        check(cursor.eat("="), "Expected = after property name")?;
        let expression = cursor
            .take("property value")?
            .scheme()
            .ok_or("Expected a literal property value")?;
        match property {
            "timing" if field == "Score.timing" && matches!(expression, "##f" | "#f") => {}
            "measureLength" => {
                let mut parts = expression
                    .trim_start_matches("#(")
                    .trim_end_matches(')')
                    .split_whitespace();
                check(
                    parts.next() == Some("ly:make-moment"),
                    "Unsupported measure length",
                )?;
                let size = Time::parse(parts.next().unwrap_or(""))
                    .filter(|value| *value > Time::ZERO)
                    .ok_or("Unsupported measure length")?;
                check(parts.next().is_none(), "Unsupported measure length")?;
                self.track.measure_lengths.insert(self.track.position, size);
            }
            // The LilyPond viewer already uses this GM program for the melody.
            "midiInstrument" if field == "Staff.midiInstrument" && expression == "#\"flute\"" => {}
            "instrumentName"
                if field == "PianoStaff.instrumentName" && expression == "#\"Piano\"" => {}
            "midiInstrument"
                if field == "PianoStaff.midiInstrument" && expression == "#\"acoustic grand\"" => {}
            "repeatCommands" => {
                let tokens = scheme_items(expression);
                self.track
                    .repeat_commands
                    .insert(self.track.position, tokens);
            }
            _ => return Err(format!("Unsupported property {field}")),
        }
        Ok(())
    }

    fn override_command(&mut self, word: &str, cursor: &mut Cursor<'_, 's>) -> Result<()> {
        let once = word == "\\once";
        let word = if once {
            cursor.take("command after once")?.word()
        } else {
            word
        };
        let target = cursor.take("property name")?.word();
        match word {
            "\\omit" if once => match target {
                "TupletBracket" => self.omit_next_bracket = true,
                "Staff.TimeSignature" => self.hide_time = true,
                _ => return Err(format!("Unsupported omission {target:?}")),
            },
            "\\revert" if !once && target == "NoteHead.style" => self.notehead = "normal",
            "\\override" => {
                check(cursor.eat("="), "Expected = after override")?;
                if target == "BreathingSign.text" && syntax::is_markup(cursor.peek_word()) {
                    cursor.next();
                    let glyph = syntax::markup_text(syntax::markup_argument(cursor)?);
                    check(
                        glyph.trim() == "scripts.caesura.straight",
                        "Unsupported breathing glyph",
                    )?;
                    self.breath_mark = "caesura";
                    return Ok(());
                }
                let value = cursor
                    .take("override value")?
                    .scheme()
                    .ok_or("Expected a literal override value")?;
                if once && target == "Glissando.style" && value == "#'zigzag" {
                    self.next_glissando = "zigzag";
                } else if target == "NoteHead.style" {
                    let shape = match value {
                        "#'slash" => "slash",
                        "#'cross" => "x",
                        "#'diamond" => "diamond",
                        "#'triangle" => "triangle",
                        _ => return Err(format!("Unsupported notehead {value:?}")),
                    };
                    if once {
                        self.next_notehead = Some(shape);
                    } else {
                        self.notehead = shape;
                    }
                } else {
                    return Err(format!("Unsupported override {target:?}"));
                }
            }
            _ => return Err(format!("Unsupported {word} for {target:?}")),
        }
        Ok(())
    }

    fn beam(&mut self, word: &str) -> Result<()> {
        let last = self
            .track
            .events
            .len()
            .checked_sub(1)
            .ok_or("Beam without a note")?;
        if word == "[" {
            check(self.beam_start.is_none(), "Nested manual beam")?;
            self.beam_start = Some(last);
            return Ok(());
        }
        let first = self.beam_start.take().ok_or("Beam end without a start")?;
        let events = &mut self.track.events[first..];
        let depths: Vec<_> = events
            .iter()
            .map(|event| {
                let (kind, _) = note_type(event.written).ok_or("Unsupported beam duration")?;
                let index = ["whole", "half", "quarter", "eighth", "16th", "32nd", "64th"]
                    .iter()
                    .position(|&name| name == kind)
                    .unwrap();
                Ok::<_, String>(index.saturating_sub(2))
            })
            .collect::<Result<_>>()?;
        for level in 1..=depths.iter().copied().max().unwrap_or(0) {
            for (index, &depth) in depths.iter().enumerate() {
                if depth < level {
                    continue;
                }
                let before = index > 0 && depths[index - 1] >= level;
                let after = depths.get(index + 1).is_some_and(|&depth| depth >= level);
                let beam = match (before, after) {
                    (true, true) => "continue",
                    (true, false) => "end",
                    (false, true) => "begin",
                    (false, false) if index > 0 => "backward hook",
                    _ => "forward hook",
                };
                events[index].beams.push((level, beam));
            }
        }
        Ok(())
    }

    fn simultaneous(&mut self, nodes: &[Node<'s>], scope: Scope) -> Result<()> {
        let separator = nodes.iter().position(|node| node.word() == "\\\\");
        let mut cursor = Cursor::new(nodes);
        let start = self.track.position;
        let (stem, lyric_voice) = (self.stem, self.lyric_voice);
        self.stem = Some("up");
        if let Some(index) = separator {
            self.lyric_voice = 1;
            self.seq(&nodes[..index], scope)?;
            cursor.index = index + 1;
        } else {
            self.one(&mut cursor, scope)?;
        }
        self.stem = stem;
        self.lyric_voice = lyric_voice;
        let mut second = Reader::new(false, self.language);
        second.drums = self.drums;
        second.duration = self.duration;
        second.relative_to = self.relative_to;
        second.track.position = start;
        second.stem = Some("down");
        second.lyric_voice = 2;
        if separator.is_some() {
            second.seq(&nodes[cursor.index..], scope)?;
        } else {
            second.one(&mut cursor, scope)?;
        }
        check(
            second.track.secondary.is_empty(),
            "Nested simultaneous voices",
        )?;
        self.track.secondary.extend(second.track.events);
        self.track.position = self.track.position.max(second.track.position);
        self.duration = second.duration;
        self.relative_to = second.relative_to;
        let mut index = 0;
        while cursor.peek().is_some() && separator.is_none() {
            let mut extra = Reader::new(false, self.language);
            extra.drums = self.drums;
            extra.duration = self.duration;
            extra.relative_to = self.relative_to;
            extra.track.position = start;
            extra.lyric_voice = u8::try_from(index + 3).map_err(|_| "Too many voices")?;
            extra.one(&mut cursor, scope)?;
            check(
                extra.track.secondary.is_empty(),
                "Nested simultaneous voices",
            )?;
            if self.track.extra_voices.len() <= index {
                self.track.extra_voices.push(Vec::new());
            }
            self.track.extra_voices[index].extend(extra.track.events);
            self.track.position = self.track.position.max(extra.track.position);
            index += 1;
        }
        Ok(())
    }

    fn repeat(&mut self, cursor: &mut Cursor<'_, 's>, scope: Scope) -> Result<()> {
        let kind = cursor.take("repeat type")?.word();
        check(
            matches!(kind, "unfold" | "volta" | "percent"),
            format!("Unsupported repeat type {kind:?}"),
        )?;
        let times = cursor
            .take("repeat count")?
            .word()
            .parse::<usize>()
            .map_err(|_| "Invalid repeat count")?;
        check(times > 0, "Invalid repeat count")?;
        let body = cursor.take("repeat body")?;
        let mut alternatives = Vec::new();
        if cursor.eat("\\alternative") {
            let list = cursor
                .take("volta endings")?
                .children(Group::Sequence)
                .ok_or("Expected an alternative music list")?;
            let mut endings = Cursor::new(list);
            let count = list
                .iter()
                .filter(|node| node.children(Group::Sequence).is_some())
                .count();
            check(
                count > 0 && count <= times,
                "Invalid number of volta endings",
            )?;
            let shared = times - count + 1;
            while endings.peek().is_some() {
                let passes = if endings.eat("\\volta") {
                    let mut values = String::new();
                    while endings
                        .peek()
                        .is_some_and(|node| node.children(Group::Sequence).is_none())
                    {
                        values.push_str(endings.next().unwrap().word());
                    }
                    values
                        .split(',')
                        .map(|n| {
                            n.parse::<usize>()
                                .map_err(|_| "Invalid explicit volta number".into())
                        })
                        .collect::<Result<_>>()?
                } else if alternatives.is_empty() {
                    (1..=shared).collect()
                } else {
                    vec![shared + alternatives.len()]
                };
                let branch = endings
                    .take("volta ending")?
                    .children(Group::Sequence)
                    .ok_or("Expected a volta ending")?;
                alternatives.push((passes, branch));
            }
        }
        if kind == "volta" {
            check(
                scope.tuplet.is_none() && scope.grace.is_none(),
                "Volta repeat in a tuplet or grace group",
            )?;
            let start = self.track.position;
            self.seq(std::slice::from_ref(body), scope)?;
            let body_end = self.track.position;
            let mut ends: Vec<(Time, Vec<usize>)> = Vec::new();
            for (passes, branch) in alternatives {
                self.seq(branch, scope)?;
                if self.track.position > ends.last().map_or(body_end, |&(end, _)| end) {
                    ends.push((self.track.position, passes));
                }
            }
            if self.track.position > start {
                self.track.repeats.push(Repeat {
                    start,
                    body_end,
                    ends,
                    times,
                });
            }
        } else {
            let body_state = (self.duration, self.relative_to);
            let mut before_ending = Vec::new();
            let mut final_state = None;
            if !alternatives.is_empty() {
                let mut reading = self.clone();
                reading.seq(std::slice::from_ref(body), scope)?;
                for (_, branch) in &alternatives {
                    before_ending.push((reading.duration, reading.relative_to));
                    reading.seq(branch, scope)?;
                }
                final_state = Some((reading.duration, reading.relative_to));
            }
            for pass in 1..=times {
                (self.duration, self.relative_to) = body_state;
                self.seq(std::slice::from_ref(body), scope)?;
                if let Some((index, (_, branch))) = alternatives
                    .iter()
                    .enumerate()
                    .find(|(_, (passes, _))| passes.contains(&pass))
                {
                    (self.duration, self.relative_to) = before_ending[index];
                    self.seq(branch, scope)?;
                }
            }
            if let Some(state) = final_state {
                (self.duration, self.relative_to) = state;
            }
        }
        Ok(())
    }
}

fn placement(prefix: char) -> Option<&'static str> {
    match prefix {
        '^' => Some("above"),
        '_' => Some("below"),
        _ => None,
    }
}

fn dynamic(word: &str) -> Option<(&'static str, &str)> {
    let text = word.strip_prefix('\\')?;
    match text {
        "<" => Some(("wedge", "crescendo")),
        ">" => Some(("wedge", "diminuendo")),
        "!" => Some(("wedge", "stop")),
        _ if (1..=6).contains(&text.len())
            && text.bytes().all(|b| matches!(b, b'f' | b'm' | b'p')) =>
        {
            Some(("dynamics", text))
        }
        "sf" | "sfp" | "sfpp" | "sffp" | "sfz" | "sffz" | "sff" | "rfz" | "fz" | "n" => {
            Some(("dynamics", text))
        }
        _ => None,
    }
}

pub fn set_layout(
    layout: &mut Vec<(&'static str, &'static str)>,
    (key, value): (&'static str, &'static str),
) {
    if let Some(item) = layout.iter_mut().find(|item| item.0 == key) {
        item.1 = value;
    } else {
        layout.push((key, value));
    }
}

fn markup_directions(
    nodes: &[Node<'_>],
    placement: Option<&'static str>,
    rehearsal: bool,
) -> Vec<Direction> {
    let mut text = syntax::markup_text(nodes).trim().to_string();
    let mut directions = Vec::new();
    for glyph in ["segno", "coda"] {
        let tag = format!("scripts.{glyph}");
        if text.contains(&tag) {
            text = text.replace(&tag, "").trim().into();
            directions.push(Direction::new(glyph, placement, ""));
        }
    }
    if !text.is_empty() {
        let kind = if rehearsal && syntax::contains(nodes, "\\box") {
            "rehearsal"
        } else {
            "words"
        };
        directions.push(Direction::new(kind, placement, text));
    }
    directions
}

fn scheme_items(expr: &str) -> Vec<String> {
    let mut values = Vec::new();
    let expr = expr.trim_start_matches(['#', '\'']);
    let mut input = expr.char_indices().peekable();
    while let Some((start, ch)) = input.next() {
        if ch.is_whitespace() || matches!(ch, '(' | ')') {
            continue;
        }
        if ch == '"' {
            let mut end = expr.len();
            while let Some((pos, c)) = input.next() {
                if c == '"' {
                    end = pos;
                    break;
                }
                if c == '\\' {
                    input.next();
                }
            }
            values.push(syntax::unquote(&expr[start + 1..end]));
        } else {
            while input
                .peek()
                .is_some_and(|&(_, c)| !c.is_whitespace() && !matches!(c, '(' | ')'))
            {
                input.next();
            }
            values.push(expr[start..input.peek().map_or(expr.len(), |x| x.0)].into());
        }
    }
    values
}

fn relative_reference(cursor: &mut Cursor<'_, '_>, language: &str) -> Pitch {
    if let Some((pitch, "")) = pitch_prefix(cursor.peek_word(), language) {
        cursor.next();
        pitch
    } else {
        Pitch {
            step: 3,
            alter: 0,
            octave: 3,
        }
    }
}

fn relative_expression<'a, 's>(cursor: &Cursor<'a, 's>) -> Option<&'a Node<'s>> {
    let mut cursor = Cursor {
        nodes: cursor.nodes,
        index: cursor.index,
    };
    // python-ly enters translator expressions, consuming their grouped music
    // before it looks for the start of the relative expression.
    while matches!(cursor.peek_word(), "\\chordmode" | "\\notemode") {
        cursor.next();
    }
    cursor.peek()
}

pub(crate) fn read_track<'s>(
    music: &[Node<'s>],
    document: &[Node<'s>],
    chords: bool,
) -> Result<Track> {
    let start = music.first().map_or(0, |node| node.start);
    let mut reader = Reader::new(chords, pitch_language(document, start));
    reader.seq(music, Scope::default())?;
    check(
        reader.next_chord_parenthesis.is_none() && !reader.alternate_chords_open,
        "Unclosed chord parenthesis",
    )?;
    Ok(reader.track)
}

#[derive(Clone, Copy)]
struct Context<'a, 's> {
    kind: &'s str,
    name: Option<&'s str>,
    contents: &'a [Node<'s>],
    start: usize,
    relative_scope: Option<(usize, Pitch)>,
}

pub(crate) fn context_head<'a, 's>(
    cursor: &mut Cursor<'a, 's>,
) -> Result<(&'s str, Option<&'s str>)> {
    let kind = cursor.take("context type")?.word();
    let label = if cursor.eat("=") {
        cursor.eat("=");
        let name = cursor.take("context name")?;
        Some(name.text().unwrap_or(name.word()))
    } else {
        None
    };
    while cursor.eat("\\with") {
        cursor.take("context settings")?;
    }
    Ok((kind, label))
}

fn contexts<'a, 's>(
    nodes: &'a [Node<'s>],
    document: &[Node<'s>],
    relative_scope: Option<(usize, Pitch)>,
    result: &mut Vec<Context<'a, 's>>,
) -> Result<()> {
    let mut relative_group = None;
    for (index, node) in nodes.iter().enumerate() {
        match node.word() {
            "\\relative" => {
                let mut cursor = Cursor::new(&nodes[index + 1..]);
                let reference =
                    relative_reference(&mut cursor, pitch_language(document, node.start));
                if let Some(expression) = relative_expression(&cursor)
                    && matches!(expression.kind, Kind::Group(_, _))
                {
                    relative_group = Some((expression.start, reference));
                }
            }
            "\\new" | "\\context" => {
                let mut cursor = Cursor::new(&nodes[index + 1..]);
                let (kind, name) = context_head(&mut cursor)?;
                result.push(Context {
                    kind,
                    name,
                    contents: &cursor.nodes[cursor.index..],
                    start: node.start,
                    relative_scope,
                });
            }
            _ => {}
        }
        if let Kind::Group(_, children) = &node.kind {
            let scope = relative_group
                .filter(|(start, _)| *start == node.start)
                .or(relative_scope);
            contexts(children, document, scope, result)?;
        }
    }
    Ok(())
}

pub struct Score<'a, 's> {
    pub melody: Track,
    pub chords: Track,
    pub info: Info,
    pub lyric_sections: Vec<(&'s str, &'a [Node<'s>])>,
    pub named_voices: Vec<&'s str>,
}

pub fn read_score<'a, 's>(nodes: &'a [Node<'s>]) -> Result<Score<'a, 's>> {
    let mut scores = Vec::new();
    collect_scores(nodes, None, &mut scores)?;
    check(
        scores.len() == 1,
        format!("Expected one score, found {}", scores.len()),
    )?;
    read_score_block(nodes, scores[0])
}

pub(crate) fn read_score_block<'a, 's>(
    nodes: &'a [Node<'s>],
    (score, book): ScoreBlock<'a, 's>,
) -> Result<Score<'a, 's>> {
    let mut declarations = Vec::new();
    contexts(score, nodes, None, &mut declarations)?;
    declarations.sort_by_key(|context| context.start);
    let mut melody = None;
    let mut chords = None;
    let mut named_voices = Vec::new();
    let mut duration = Duration::default();
    let mut relative_pitches = BTreeMap::new();
    for declaration in declarations.iter() {
        match declaration.kind {
            "FretBoards" => return Err("Guitar fretboard diagrams are not supported yet".into()),
            "Staff" | "ChordNames" => {
                let is_chords = declaration.kind == "ChordNames";
                let track = if is_chords { &mut chords } else { &mut melody };
                check(
                    track.is_none(),
                    format!("Expected one {} context", declaration.kind),
                )?;
                let language = pitch_language(nodes, declaration.start);
                let mut reader = Reader::new(is_chords, language);
                reader.duration = duration;
                if let Some((scope, reference)) = declaration.relative_scope {
                    reader.relative_to = Some(*relative_pitches.get(&scope).unwrap_or(&reference));
                }
                reader.one(&mut Cursor::new(declaration.contents), Scope::default())?;
                check(
                    reader.next_chord_parenthesis.is_none() && !reader.alternate_chords_open,
                    "Unclosed chord parenthesis",
                )?;
                duration = reader.duration;
                if let (Some((scope, _)), Some(pitch)) =
                    (declaration.relative_scope, reader.relative_to)
                {
                    relative_pitches.insert(scope, pitch);
                }
                *track = Some(reader.track);
            }
            "Voice" => {
                if let Some(name) = declaration.name
                    && !name.is_empty()
                    && !named_voices.contains(&name)
                {
                    named_voices.push(name);
                }
            }
            "Lyrics" => {
                check(declaration.contents.first().is_some_and(|node| {
                    node.word() == "\\lyricsto" ||
                    matches!(&node.kind,Kind::Group(_, children) if syntax::contains(children,"\\lyricsto"))
                }),"Lyrics must name their melody voice with lyricsto")?;
            }
            _ => {}
        }
    }
    check(
        melody.is_some() || chords.is_some(),
        "No music staff or chords",
    )?;
    let mut melody = melody.unwrap_or_else(|| Track {
        meters: chords
            .as_ref()
            .map(|track| track.meters.clone())
            .unwrap_or_default(),
        ..Track::default()
    });
    if melody.events.is_empty()
        && let Some(chords) = chords.as_ref()
    {
        melody.events = chords
            .events
            .iter()
            .filter(|event| event.length() != Time::ZERO)
            .cloned()
            .map(|mut event| {
                event.invisible = true;
                event
            })
            .collect();
        melody.position = chords.position;
        melody.pickup = chords.pickup;
        melody.repeats = chords.repeats.clone();
    }
    let chords = chords.unwrap_or_else(|| Track {
        position: melody.position,
        pickup: melody.pickup,
        repeats: melody.repeats.clone(),
        ..Track::default()
    });
    let mut lyric_sections = Vec::new();
    collect_lyrics(score, &mut lyric_sections)?;
    Ok(Score {
        melody,
        chords,
        info: book.map_or_else(Info::default, |nodes| read_info(nodes)),
        lyric_sections,
        named_voices,
    })
}

fn known_language(name: &str) -> bool {
    matches!(
        name,
        "nederlands"
            | "english"
            | "deutsch"
            | "italiano"
            | "svenska"
            | "norsk"
            | "suomi"
            | "espanol"
            | "portugues"
            | "vlaams"
            | "catalan"
    )
}

fn pitch_language<'s>(document: &[Node<'s>], before: usize) -> &'s str {
    fn latest<'s>(nodes: &[Node<'s>], before: usize) -> Option<&'s str> {
        let end = nodes.partition_point(|node| node.start < before);
        for (index, node) in nodes[..end].iter().enumerate().rev() {
            if let Kind::Group(_, children) = &node.kind
                && let Some(language) = latest(children, before)
            {
                return Some(language);
            }
            if let Some(label) = nodes.get(index + 1).and_then(Node::text) {
                let name = match node.word() {
                    "\\language" => Some(label),
                    "\\include" => label.strip_suffix(".ly"),
                    _ => None,
                };
                if let Some(name) = name.filter(|name| known_language(name)) {
                    return Some(name);
                }
            }
        }
        None
    }
    latest(document, before).unwrap_or("nederlands")
}

pub(crate) type ScoreBlock<'a, 's> = (&'a [Node<'s>], Option<&'a [Node<'s>]>);

pub(crate) fn collect_scores<'a, 's>(
    nodes: &'a [Node<'s>],
    book: Option<&'a [Node<'s>]>,
    out: &mut Vec<ScoreBlock<'a, 's>>,
) -> Result<()> {
    let mut cursor = Cursor::new(nodes);
    while let Some(node) = cursor.next() {
        match node.word() {
            "\\bookpart" => {
                let branch = cursor
                    .take("book part")?
                    .children(Group::Sequence)
                    .ok_or("Expected a book part")?;
                collect_scores(branch, Some(branch), out)?;
            }
            "\\score" => {
                let branch = cursor
                    .take("score")?
                    .children(Group::Sequence)
                    .ok_or("Expected a score")?;
                out.push((branch, book));
            }
            _ => {
                if let Kind::Group(_, children) = &node.kind {
                    collect_scores(children, book, out)?;
                }
            }
        }
    }
    Ok(())
}

fn collect_lyrics<'a, 's>(
    nodes: &'a [Node<'s>],
    out: &mut Vec<(&'s str, &'a [Node<'s>])>,
) -> Result<()> {
    let mut cursor = Cursor::new(nodes);
    while let Some(node) = cursor.next() {
        if node.word() == "\\lyricsto" {
            let name = cursor.take("lyric voice")?;
            let name = name.text().unwrap_or(name.word());
            cursor.eat("\\lyricmode");
            let lyric = cursor
                .take("lyrics")?
                .children(Group::Sequence)
                .ok_or("Expected lyrics")?;
            out.push((name, lyric));
        } else if let Kind::Group(_, children) = &node.kind {
            collect_lyrics(children, out)?;
        }
    }
    Ok(())
}

pub(crate) fn read_info(nodes: &[Node<'_>]) -> Info {
    let mut info = Info::default();
    let mut before_score = true;
    let mut cursor = Cursor::new(nodes);
    while let Some(node) = cursor.next() {
        if node.word() == "\\score" {
            before_score = false;
            cursor.next();
        } else if syntax::is_markup(node.word())
            && let Ok(children) = syntax::markup_argument(&mut cursor)
        {
            fill_lines(children, before_score, &mut info);
        }
    }
    if info.title.is_some() {
        info
    } else {
        Info::default()
    }
}

fn fill_lines(nodes: &[Node<'_>], before: bool, info: &mut Info) {
    for (index, node) in nodes.iter().enumerate() {
        if node.word() == "\\fill-line" {
            let mut cursor = Cursor::new(&nodes[index + 1..]);
            if let Ok(line) = syntax::markup_argument(&mut cursor) {
                let is_title = syntax::contains(line, "\\larger");
                let mut texts = Vec::new();
                strings(line, &mut texts);
                for text in texts.into_iter().filter(|s| !s.is_empty()) {
                    if !before {
                        if let Some(text) = text.strip_prefix("Typeset by ") {
                            info.engraver = Some(text.into());
                        } else {
                            info.rights.push(text);
                        }
                    } else if is_title && info.title.is_none() {
                        info.title = Some(text);
                    } else if is_title && info.subtitle.is_none() {
                        info.subtitle = Some(text);
                    } else if let Some(text) = text.strip_prefix("Music by ") {
                        info.composer = Some(text.into());
                    } else if let Some(text) = text.strip_prefix("Lyrics by ") {
                        info.lyricist = Some(text.into());
                    } else if let Some(text) = text.strip_prefix("Lyrics and Music by ") {
                        info.composer = Some(text.into());
                        info.lyricist = Some(text.into());
                    } else {
                        info.headings.push(text);
                    }
                }
            }
        }
        if let Kind::Group(_, children) = &node.kind {
            fill_lines(children, before, info);
        }
    }
}

fn strings(nodes: &[Node<'_>], out: &mut Vec<String>) {
    for node in nodes {
        match &node.kind {
            Kind::Text(text) => out.push(syntax::unquote(text)),
            Kind::Scheme(text) => out.extend(syntax::scheme_strings(text)),
            Kind::Group(_, children) => strings(children, out),
            _ => {}
        }
    }
}
