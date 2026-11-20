use std::cmp::Ordering;
use std::collections::{BTreeMap, BTreeSet};
use std::fmt;
use std::ops::{Add, AddAssign, Div, Mul, Sub};

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct Time {
    pub numerator: i64,
    pub denominator: i64,
}

impl Time {
    pub const ZERO: Self = Self::integer(0);
    pub const ONE: Self = Self::integer(1);

    pub const fn integer(numerator: i64) -> Self {
        Self {
            numerator,
            denominator: 1,
        }
    }

    pub fn new(numerator: i64, denominator: i64) -> Self {
        assert_ne!(denominator, 0, "zero denominator");
        let divisor = gcd(numerator, denominator) * denominator.signum();
        Self {
            numerator: numerator / divisor,
            denominator: denominator / divisor,
        }
    }

    pub fn parse(text: &str) -> Option<Self> {
        let (numerator, denominator) = text.split_once('/')?;
        let n = numerator.parse().ok()?;
        let d = denominator.parse().ok()?;
        (d > 0).then(|| Self::new(n, d))
    }

    pub fn units(self, divisions: i64) -> i64 {
        let value = self * Self::integer(4 * divisions);
        value.numerator / value.denominator
    }
}

impl Default for Time {
    fn default() -> Self {
        Self::ZERO
    }
}
impl Ord for Time {
    fn cmp(&self, other: &Self) -> Ordering {
        (i128::from(self.numerator) * i128::from(other.denominator))
            .cmp(&(i128::from(other.numerator) * i128::from(self.denominator)))
    }
}
impl PartialOrd for Time {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}
impl Add for Time {
    type Output = Self;
    fn add(self, rhs: Self) -> Self {
        let divisor = gcd(self.denominator, rhs.denominator);
        Self::new(
            self.numerator * (rhs.denominator / divisor)
                + rhs.numerator * (self.denominator / divisor),
            self.denominator / divisor * rhs.denominator,
        )
    }
}
impl AddAssign for Time {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs;
    }
}
impl Sub for Time {
    type Output = Self;
    fn sub(self, rhs: Self) -> Self {
        self + Self::new(-rhs.numerator, rhs.denominator)
    }
}
impl Mul for Time {
    type Output = Self;
    fn mul(self, rhs: Self) -> Self {
        let a = gcd(self.numerator, rhs.denominator);
        let b = gcd(rhs.numerator, self.denominator);
        Self::new(
            (self.numerator / a) * (rhs.numerator / b),
            (self.denominator / b) * (rhs.denominator / a),
        )
    }
}
impl Div for Time {
    type Output = Self;
    fn div(self, rhs: Self) -> Self {
        let a = gcd(self.numerator, rhs.numerator);
        let b = gcd(rhs.denominator, self.denominator);
        Self::new(
            (self.numerator / a) * (rhs.denominator / b),
            (self.denominator / b) * (rhs.numerator / a),
        )
    }
}
impl fmt::Display for Time {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.denominator == 1 {
            write!(f, "{}", self.numerator)
        } else {
            write!(f, "{}/{}", self.numerator, self.denominator)
        }
    }
}

pub fn gcd(mut a: i64, mut b: i64) -> i64 {
    while b != 0 {
        (a, b) = (b, a % b);
    }
    a.abs()
}

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct Pitch {
    pub step: usize,
    pub alter: i8,
    pub octave: i32,
}

impl Pitch {
    pub fn step_name(self) -> char {
        b"CDEFGAB"[self.step] as char
    }

    pub fn semitone(self) -> i32 {
        12 * self.octave + [0, 2, 4, 5, 7, 9, 11][self.step] + i32::from(self.alter)
    }
}

#[derive(Clone, Debug)]
pub struct Member {
    pub pitch: Pitch,
    pub parentheses: bool,
    pub harmonic: bool,
}

#[derive(Clone, Debug)]
pub struct Harmony {
    pub quality: String,
    pub bass: Option<Pitch>,
    pub spec: String,
}

#[derive(Clone, Debug)]
pub struct Direction {
    pub kind: &'static str,
    pub placement: Option<&'static str>,
    pub text: String,
}

impl Direction {
    pub fn new(
        kind: &'static str,
        placement: Option<&'static str>,
        text: impl Into<String>,
    ) -> Self {
        Self {
            kind,
            placement,
            text: text.into(),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Mark {
    pub group: &'static str,
    pub value: String,
    pub placement: Option<&'static str>,
}

#[derive(Clone, Debug, Default)]
pub struct Event {
    pub position: Time,
    pub written: Time,
    pub scale: Time,
    pub members: Vec<Member>,
    pub rest: Option<char>,
    pub measure_rest: bool,
    pub invisible: bool,
    pub grace: Option<bool>,
    pub stem: Option<&'static str>,
    pub lyric_voice: u8,
    pub tuplet: Option<Time>,
    pub tuplet_bracket: bool,
    pub tuplet_start: bool,
    pub tuplet_stop: bool,
    pub tied: bool,
    pub tie_starts: BTreeSet<Pitch>,
    pub tie_stops: BTreeSet<Pitch>,
    pub open_ties: BTreeSet<Pitch>,
    pub harmony: Option<Harmony>,
    pub chord_parenthesis: Option<&'static str>,
    pub alternate_chord: bool,
    pub directions: Vec<Direction>,
    pub markings: Vec<Mark>,
    pub notehead: &'static str,
    pub parenthesized: bool,
    pub repeat_tie: bool,
    pub let_ring: bool,
    pub glissando_start: Option<&'static str>,
    pub glissando_stop: Option<&'static str>,
    pub slur_continuation: bool,
    pub slurs: Vec<(&'static str, usize, Option<&'static str>)>,
    pub beams: Vec<(usize, &'static str)>,
}

impl Event {
    pub fn length(&self) -> Time {
        if self.grace.is_some() {
            Time::ZERO
        } else {
            self.written * self.scale * self.tuplet.unwrap_or(Time::ONE)
        }
    }

    pub fn pitched(&self) -> bool {
        !self.invisible && !self.members.is_empty()
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Repeat {
    pub start: Time,
    pub body_end: Time,
    pub ends: Vec<(Time, Vec<usize>)>,
    pub times: usize,
}

#[derive(Clone, Copy, Debug)]
pub struct Meter {
    pub beats: i64,
    pub beat_type: i64,
    pub numeric: bool,
    pub hidden: bool,
}

impl Default for Meter {
    fn default() -> Self {
        Self {
            beats: 4,
            beat_type: 4,
            numeric: false,
            hidden: false,
        }
    }
}

impl Meter {
    pub fn length(&self) -> Time {
        Time::new(self.beats, self.beat_type)
    }
}

#[derive(Clone, Debug)]
pub struct Tempo {
    pub has_text: bool,
    pub words: Option<String>,
    pub beat: Time,
    pub bpm: Option<(i64, Option<i64>)>,
}

#[derive(Clone, Debug, Default)]
pub struct Track {
    pub position: Time,
    pub pickup: Time,
    pub meters: BTreeMap<Time, Meter>,
    pub keys: BTreeMap<Time, (i32, &'static str)>,
    pub compressed_rests: BTreeMap<Time, Time>,
    pub measure_lengths: BTreeMap<Time, Time>,
    pub repeat_commands: BTreeMap<Time, Vec<String>>,
    pub transposition: Option<(usize, i32, i32)>,
    pub clefs: BTreeMap<Time, (&'static str, i32, i32)>,
    pub tempos: BTreeMap<Time, Tempo>,
    pub events: Vec<Event>,
    pub secondary: Vec<Event>,
    pub repeats: Vec<Repeat>,
    pub final_barlines: BTreeSet<Time>,
    pub bar_checks: Vec<Time>,
    pub marks: BTreeMap<Time, String>,
    pub breaks: BTreeMap<Time, Vec<(&'static str, &'static str)>>,
    pub navigation: BTreeMap<Time, Vec<&'static str>>,
    pub directions: BTreeMap<Time, Vec<Direction>>,
    pub chord_changes: bool,
}

#[derive(Debug, Default)]
pub struct Info {
    pub title: Option<String>,
    pub subtitle: Option<String>,
    pub composer: Option<String>,
    pub lyricist: Option<String>,
    pub engraver: Option<String>,
    pub headings: Vec<String>,
    pub rights: Vec<String>,
}

#[derive(Debug)]
pub struct Syllable {
    pub text: String,
    pub follows_hyphen: bool,
    pub followed_by_hyphen: bool,
    pub extend: bool,
    pub extend_type: Option<&'static str>,
    pub italic: bool,
}

impl Syllable {
    pub fn syllabic(&self) -> &'static str {
        match (self.follows_hyphen, self.followed_by_hyphen) {
            (true, true) => "middle",
            (true, false) => "end",
            (false, true) => "begin",
            (false, false) => "single",
        }
    }
}

#[derive(Default)]
pub struct Lyrics {
    pub syllables: BTreeMap<(u8, Time), Vec<(usize, Syllable)>>,
    pub stops: BTreeMap<(u8, Time), Vec<usize>>,
    pub unplaced: Vec<(usize, Vec<String>)>,
}

pub fn note_type(length: Time) -> Option<(&'static str, usize)> {
    let types = ["whole", "half", "quarter", "eighth", "16th", "32nd", "64th"];
    for (index, kind) in types.into_iter().enumerate() {
        let base = Time::new(1, 1 << index);
        for (dots, scale) in [Time::ONE, Time::new(3, 2), Time::new(7, 4)]
            .into_iter()
            .enumerate()
        {
            if length == base * scale {
                return Some((kind, dots));
            }
        }
    }
    None
}
