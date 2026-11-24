use crate::Result;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Group {
    Sequence,
    Simultaneous,
    Chord,
}

#[derive(Clone, Debug)]
pub enum Kind<'s> {
    Word(&'s str),
    Text(&'s str),
    Scheme(&'s str),
    MarkupScore,
    Group(Group, Vec<Node<'s>>),
}

#[derive(Clone, Debug)]
pub struct Node<'s> {
    pub kind: Kind<'s>,
    pub start: usize,
    pub end: usize,
}

impl<'s> Node<'s> {
    pub fn word(&self) -> &'s str {
        match self.kind {
            Kind::Word(word) => word,
            _ => "",
        }
    }

    pub fn text(&self) -> Option<&'s str> {
        match self.kind {
            Kind::Text(text) => Some(text),
            _ => None,
        }
    }

    pub fn scheme(&self) -> Option<&'s str> {
        match self.kind {
            Kind::Scheme(text) => Some(text),
            _ => None,
        }
    }

    pub fn children(&self, kind: Group) -> Option<&[Node<'s>]> {
        match &self.kind {
            Kind::Group(group, children) if *group == kind => Some(children),
            _ => None,
        }
    }

    pub fn describe(&self) -> String {
        match &self.kind {
            Kind::Word(word) | Kind::Text(word) | Kind::Scheme(word) => (*word).into(),
            Kind::Group(kind, _) => format!("{kind:?}"),
            Kind::MarkupScore => "\\score".into(),
        }
    }
}

pub struct Cursor<'a, 's> {
    pub nodes: &'a [Node<'s>],
    pub index: usize,
}

impl<'a, 's> Cursor<'a, 's> {
    pub fn new(nodes: &'a [Node<'s>]) -> Self {
        Self { nodes, index: 0 }
    }

    pub fn peek(&self) -> Option<&'a Node<'s>> {
        self.nodes.get(self.index)
    }

    pub fn peek_word(&self) -> &'s str {
        self.peek().map_or("", Node::word)
    }

    pub fn next(&mut self) -> Option<&'a Node<'s>> {
        let node = self.peek()?;
        self.index += 1;
        Some(node)
    }

    pub fn take(&mut self, what: &str) -> Result<&'a Node<'s>> {
        self.next().ok_or_else(|| format!("Expected {what}"))
    }

    pub fn eat(&mut self, word: &str) -> bool {
        if self.peek_word() == word {
            self.next();
            true
        } else {
            false
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Mode {
    Music,
    Lyric,
    Markup,
}

struct Lexer<'s> {
    source: &'s str,
    pos: usize,
}

impl<'s> Lexer<'s> {
    fn rest(&self) -> &'s str {
        &self.source[self.pos..]
    }

    fn advance(&mut self) -> Option<char> {
        let ch = self.rest().chars().next()?;
        self.pos += ch.len_utf8();
        Some(ch)
    }

    fn eat(&mut self, text: &str) -> bool {
        if self.rest().starts_with(text) {
            self.pos += text.len();
            true
        } else {
            false
        }
    }

    fn skip_space(&mut self) -> Result<()> {
        loop {
            while self.rest().starts_with(char::is_whitespace) {
                self.advance();
            }
            if self.eat("%{") {
                let mut depth = 1;
                while depth > 0 {
                    if self.eat("%{") {
                        depth += 1;
                    } else if self.eat("%}") {
                        depth -= 1;
                    } else if self.advance().is_none() {
                        return Err("Unclosed block comment".into());
                    }
                }
            } else if self.eat("%") {
                while self.advance().is_some_and(|ch| ch != '\n') {}
            } else {
                return Ok(());
            }
        }
    }

    fn string(&mut self) -> Result<&'s str> {
        let start = self.pos;
        loop {
            match self.advance() {
                Some('"') => return Ok(&self.source[start..self.pos - 1]),
                Some('\\') => {
                    self.advance();
                }
                Some(_) => {}
                None => return Err(format!("Unclosed string at source character {start}")),
            }
        }
    }

    fn scheme(&mut self) -> Result<()> {
        while self.eat("#") {}
        if self.rest().starts_with(['\'', '\x60', ',']) {
            self.advance();
        }
        if self.eat("(") {
            let mut depth = 1;
            while depth > 0 {
                match self.advance() {
                    Some('(') => depth += 1,
                    Some(')') => depth -= 1,
                    Some('"') => {
                        self.string()?;
                    }
                    Some(';') => while self.advance().is_some_and(|ch| ch != '\n') {},
                    Some(_) => {}
                    None => return Err("Unclosed Scheme expression".into()),
                }
            }
        } else if self.eat("\"") {
            self.string()?;
        } else {
            while self
                .rest()
                .chars()
                .next()
                .is_some_and(|ch| !delimiter(ch) && ch != '\\')
            {
                self.advance();
            }
        }
        Ok(())
    }

    fn nodes(&mut self, close: &str, depth: u16, mode: Mode) -> Result<Vec<Node<'s>>> {
        if depth > 512 {
            return Err("LilyPond grouping is too deeply nested".into());
        }
        let mut result: Vec<Node<'s>> = Vec::new();
        loop {
            self.skip_space()?;
            if self.rest().is_empty() {
                return if close.is_empty() {
                    Ok(result)
                } else {
                    Err(format!("Expected {close}"))
                };
            }
            if !close.is_empty() && self.eat(close) {
                return Ok(result);
            }
            let start = self.pos;
            let ch = self.advance().unwrap();
            let child_mode = if mode == Mode::Music
                && (result.last().is_some_and(|n| n.word() == "\\lyricmode")
                    || result
                        .iter()
                        .rev()
                        .nth(1)
                        .is_some_and(|n| n.word() == "\\lyricsto"))
            {
                Mode::Lyric
            } else {
                mode
            };
            let kind = match ch {
                '{' => Kind::Group(Group::Sequence, self.nodes("}", depth + 1, child_mode)?),
                '<' if mode == Mode::Music && self.eat("<") => Kind::Group(
                    Group::Simultaneous,
                    self.nodes(">>", depth + 1, Mode::Music)?,
                ),
                '<' if mode == Mode::Music
                    && !result.last().is_some_and(|node| {
                        node.end == start && node.word().ends_with(['-', '^', '_'])
                    }) =>
                {
                    Kind::Group(Group::Chord, self.nodes(">", depth + 1, Mode::Music)?)
                }
                '"' => Kind::Text(self.string()?),
                '#' | '$' => {
                    self.scheme()?;
                    Kind::Scheme(&self.source[start..self.pos])
                }
                '\\' => {
                    self.command();
                    let word = &self.source[start..self.pos];
                    if is_markup(word) && mode != Mode::Markup {
                        result.push(Node {
                            kind: Kind::Word(word),
                            start,
                            end: self.pos,
                        });
                        self.markup_node(&mut result, depth + 1)?;
                        continue;
                    }
                    if mode == Mode::Markup && word == "\\score" {
                        self.markup_score(depth)?
                    } else {
                        Kind::Word(word)
                    }
                }
                '}' => {
                    return Err(format!(
                        "Unexpected closing brace at source character {start}"
                    ));
                }
                '=' if mode != Mode::Markup => Kind::Word(&self.source[start..self.pos]),
                '(' | ')' | '[' | ']' | '|' | '~' | '<' | '>' if mode == Mode::Music => {
                    Kind::Word(&self.source[start..self.pos])
                }
                _ => {
                    while self.rest().chars().next().is_some_and(|ch| match mode {
                        Mode::Music => !delimiter(ch),
                        Mode::Lyric => {
                            !ch.is_whitespace()
                                && !matches!(ch, '{' | '}' | '"' | '\\' | '#' | '%' | '=')
                        }
                        Mode::Markup => !markup_delimiter(ch),
                    }) {
                        self.advance();
                    }
                    Kind::Word(&self.source[start..self.pos])
                }
            };
            result.push(Node {
                kind,
                start,
                end: self.pos,
            });
        }
    }

    fn command(&mut self) {
        if self.rest().starts_with(|c: char| c.is_ascii_alphabetic()) {
            while self
                .rest()
                .starts_with(|c: char| c.is_ascii_alphabetic() || c == '-')
            {
                self.advance();
            }
        } else {
            self.advance();
        }
    }

    fn markup_score(&mut self, depth: u16) -> Result<Kind<'s>> {
        self.skip_space()?;
        if !self.eat("{") {
            return Err("Expected a score in markup".into());
        }
        self.nodes("}", depth + 1, Mode::Music)?;
        Ok(Kind::MarkupScore)
    }

    fn markup_node(&mut self, output: &mut Vec<Node<'s>>, depth: u16) -> Result<()> {
        if depth > 512 {
            return Err("LilyPond markup is too deeply nested".into());
        }
        self.skip_space()?;
        let start = self.pos;
        let kind = match self.advance() {
            Some('{') => Kind::Group(Group::Sequence, self.nodes("}", depth + 1, Mode::Markup)?),
            Some('"') => Kind::Text(self.string()?),
            Some('#') | Some('$') => {
                self.scheme()?;
                Kind::Scheme(&self.source[start..self.pos])
            }
            Some('\\') => {
                self.command();
                let word = &self.source[start..self.pos];
                if word == "\\score" {
                    self.markup_score(depth)?
                } else {
                    output.push(Node {
                        kind: Kind::Word(word),
                        start,
                        end: self.pos,
                    });
                    for _ in 0..markup_arity(word) {
                        self.markup_node(output, depth + 1)?;
                    }
                    return Ok(());
                }
            }
            Some('}') | None => return Err(format!("Expected markup at source character {start}")),
            Some(_) => {
                while self
                    .rest()
                    .chars()
                    .next()
                    .is_some_and(|ch| !markup_delimiter(ch))
                {
                    self.advance();
                }
                Kind::Word(&self.source[start..self.pos])
            }
        };
        output.push(Node {
            kind,
            start,
            end: self.pos,
        });
        Ok(())
    }
}

fn markup_delimiter(ch: char) -> bool {
    ch.is_whitespace() || matches!(ch, '{' | '}' | '"' | '\\' | '#' | '%' | '$')
}

fn delimiter(ch: char) -> bool {
    ch.is_whitespace()
        || matches!(
            ch,
            '{' | '}'
                | '<'
                | '>'
                | '('
                | ')'
                | '['
                | ']'
                | '|'
                | '~'
                | '"'
                | '\\'
                | '%'
                | '#'
                | '$'
                | '='
        )
}

pub fn lex(source: &str) -> Result<Vec<Node<'_>>> {
    Lexer { source, pos: 0 }.nodes("", 0, Mode::Music)
}

pub fn unquote(text: &str) -> String {
    // The prototype's String.value uses LilyPond's escaped quote and backslash.
    let mut out = String::with_capacity(text.len());
    let mut chars = text.chars().peekable();
    while let Some(ch) = chars.next() {
        if ch == '\\' && chars.peek().is_some_and(|next| matches!(next, '"' | '\\')) {
            out.push(chars.next().unwrap());
        } else {
            out.push(ch);
        }
    }
    out
}

pub fn markup_argument<'a, 's>(cursor: &mut Cursor<'a, 's>) -> Result<&'a [Node<'s>]> {
    let first = cursor.index;
    take_markup(cursor)?;
    Ok(&cursor.nodes[first..cursor.index])
}

fn take_markup(cursor: &mut Cursor<'_, '_>) -> Result<()> {
    let node = cursor.take("markup")?;
    let arity = markup_arity(node.word());
    for _ in 0..arity {
        take_markup(cursor)?;
    }
    Ok(())
}

pub fn is_markup(word: &str) -> bool {
    matches!(word, "\\markup" | "\\markuplist" | "\\markuplines")
}

// Command signatures follow python-ly 0.9.10 (ly/words.py, GPL-3.0-or-later).
fn markup_arity(word: &str) -> usize {
    match word.strip_prefix('\\').unwrap_or("") {
        "accidental"
        | "backslashed-digit"
        | "bold"
        | "box"
        | "bracket"
        | "caps"
        | "center-align"
        | "center-column"
        | "char"
        | "circle"
        | "column"
        | "compound-meter"
        | "concat"
        | "dir-column"
        | "discant"
        | "draw-dashed-line"
        | "draw-dotted-line"
        | "draw-hline"
        | "draw-line"
        | "dynamic"
        | "ellipse"
        | "figured-bass"
        | "fill-line"
        | "finger"
        | "first-visible"
        | "fontCaps"
        | "freeBass"
        | "fret-diagram"
        | "fret-diagram-terse"
        | "fret-diagram-verbose"
        | "fromproperty"
        | "harp-pedal"
        | "hbracket"
        | "hspace"
        | "huge"
        | "italic"
        | "justify"
        | "justify-field"
        | "justify-line"
        | "justify-string"
        | "large"
        | "larger"
        | "left-align"
        | "left-brace"
        | "left-column"
        | "line"
        | "lookup"
        | "markalphabet"
        | "markletter"
        | "medium"
        | "multi-measure-rest-by-number"
        | "musicglyph"
        | "normal-size-sub"
        | "normal-size-super"
        | "normal-text"
        | "normalsize"
        | "number"
        | "oval"
        | "overlay"
        | "overtie"
        | "polygon"
        | "postscript"
        | "property-recursive"
        | "rest"
        | "rhythm"
        | "right-align"
        | "right-brace"
        | "right-column"
        | "roman"
        | "rounded-box"
        | "sans"
        | "score"
        | "score-lines"
        | "simple"
        | "slashed-digit"
        | "small"
        | "smallCaps"
        | "smaller"
        | "stdBass"
        | "stdBassIV"
        | "stdBassV"
        | "stdBassVI"
        | "stencil"
        | "string-lines"
        | "sub"
        | "super"
        | "teeny"
        | "text"
        | "tie"
        | "tied-lyric"
        | "tiny"
        | "transparent"
        | "triangle"
        | "typewriter"
        | "underline"
        | "undertie"
        | "upright"
        | "vcenter"
        | "verbatim-file"
        | "vspace"
        | "whiteout"
        | "with-true-dimensions"
        | "wordwrap"
        | "wordwrap-field"
        | "wordwrap-string" => 1,
        "abs-fontsize"
        | "auto-footnote"
        | "combine"
        | "customTabClef"
        | "fontsize"
        | "footnote"
        | "fraction"
        | "halign"
        | "hcenter-in"
        | "if"
        | "lower"
        | "magnify"
        | "note"
        | "on-the-fly"
        | "override"
        | "pad-around"
        | "pad-markup"
        | "pad-x"
        | "page-link"
        | "path"
        | "raise"
        | "replace"
        | "rest-by-number"
        | "rotate"
        | "scale"
        | "table"
        | "translate"
        | "translate-scaled"
        | "unless"
        | "with-color"
        | "with-dimensions-from"
        | "with-link"
        | "with-outline"
        | "with-string-transformer"
        | "with-true-dimension"
        | "with-url"
        | "woodwind-diagram" => 2,
        "arrow-head"
        | "beam"
        | "draw-circle"
        | "draw-squiggle-line"
        | "epsfile"
        | "filled-box"
        | "general-align"
        | "note-by-number"
        | "pad-to-box"
        | "page-ref"
        | "with-dimension"
        | "with-dimension-from"
        | "with-dimensions" => 3,
        "pattern" | "put-adjacent" => 4,
        "align-on-other" | "fill-with-pattern" => 5,
        _ => 0,
    }
}

fn is_zero_argument_markup(word: &str) -> bool {
    matches!(
        word.strip_prefix('\\').unwrap_or(""),
        "coda"
            | "doubleflat"
            | "doublesharp"
            | "eyeglasses"
            | "fermata"
            | "flat"
            | "natural"
            | "null"
            | "segno"
            | "semiflat"
            | "semisharp"
            | "sesquiflat"
            | "sesquisharp"
            | "sharp"
            | "strut"
            | "table-of-contents"
            | "varcoda"
    )
}

pub fn markup_text(nodes: &[Node<'_>]) -> String {
    markup_list(nodes, " ")
}

fn markup_list(nodes: &[Node<'_>], separator: &str) -> String {
    let mut cursor = Cursor::new(nodes);
    let mut parts = Vec::new();
    while cursor.peek().is_some() {
        parts.push(render_markup_node(&mut cursor));
    }
    parts.join(separator)
}

fn render_markup_node(cursor: &mut Cursor<'_, '_>) -> String {
    let Some(node) = cursor.next() else {
        return String::new();
    };
    match &node.kind {
        Kind::Text(text) => unquote(text),
        Kind::Scheme(text) => scheme_strings(text).concat(),
        Kind::MarkupScore => String::new(),
        Kind::Group(_, children) => markup_list(children, " "),
        Kind::Word(word) if word.starts_with('\\') => {
            if is_zero_argument_markup(word) {
                // The reference AST collects the remainder of the current
                // markup list underneath a zero-argument built-in.
                let tail = &cursor.nodes[cursor.index..];
                cursor.index = cursor.nodes.len();
                return markup_list(tail, " ");
            }
            let arity = markup_arity(word);
            let separator = if *word == "\\concat" { "" } else { " " };
            if arity == 1
                && let Some(Kind::Group(_, children)) = cursor.peek().map(|node| &node.kind)
            {
                cursor.next();
                return markup_list(children, separator);
            }
            (0..arity)
                .map(|_| render_markup_node(cursor))
                .collect::<Vec<_>>()
                .join(separator)
        }
        Kind::Word(word) => (*word).into(),
    }
}

pub fn scheme_strings(source: &str) -> Vec<String> {
    let mut lexer = Lexer { source, pos: 0 };
    let mut text = Vec::new();
    while let Some(ch) = lexer.advance() {
        match ch {
            '"' => {
                if let Ok(value) = lexer.string() {
                    text.push(unquote(value));
                }
            }
            ';' => while lexer.advance().is_some_and(|ch| ch != '\n') {},
            _ => {}
        }
    }
    text
}

pub fn contains(nodes: &[Node<'_>], word: &str) -> bool {
    nodes.iter().any(|node| {
        node.word() == word
            || matches!(&node.kind, Kind::Group(_, children) if contains(children, word))
    })
}
