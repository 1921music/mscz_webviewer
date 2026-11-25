mod ensemble;
mod fretboard;
mod lilypond;
mod model;
mod musicxml;
mod score;
mod syntax;

pub type Result<T> = std::result::Result<T, String>;

fn check(condition: bool, message: impl Into<String>) -> Result<()> {
    if condition {
        Ok(())
    } else {
        Err(message.into())
    }
}

#[derive(Default)]
pub struct Options<'a> {
    pub title: Option<&'a str>,
    pub measure_numbers: Option<&'a str>,
    pub page_breaks: &'a [i64],
}

fn bookparts(source: &str) -> impl Iterator<Item = (usize, &str)> {
    let mut byte = 0;
    source.split_inclusive('\n').filter_map(move |line| {
        let start = byte;
        byte += line.len();
        let command = line.trim_start_matches([' ', '\t']);
        command
            .strip_prefix("\\bookpart")
            .filter(|tail| tail.trim_start_matches([' ', '\t']).starts_with('{'))
            .map(|_| (start, line))
    })
}

fn toc_label(source: &str) -> Option<&str> {
    let suffix = source.strip_prefix("\\tocItem")?;
    let suffix = suffix
        .strip_prefix(char::is_whitespace)?
        .trim_start()
        .strip_prefix("\\markup")?;
    suffix
        .strip_prefix(char::is_whitespace)?
        .trim_start()
        .strip_prefix('"')
}

fn select_title<'a>(source: &'a str, title: &str) -> Result<&'a str> {
    let mut pos = 0;
    let mut has_toc = false;
    let mut found = Vec::new();
    for line in source.split_inclusive('\n') {
        if let Some(label) = toc_label(&source[pos..]) {
            has_toc = true;
            if label
                .strip_prefix(title)
                .is_some_and(|tail| tail.starts_with('"') || tail.starts_with(" / "))
            {
                found.push(pos);
            }
        }
        pos += line.len();
    }
    if !has_toc {
        return Ok(source);
    }
    check(
        found.len() == 1,
        format!(
            "Expected one OpenBook entry named {title:?}, found {}",
            found.len()
        ),
    )?;
    let preceding = bookparts(source)
        .map(|(position, _)| position)
        .take_while(|&p| p < found[0])
        .last()
        .ok_or_else(|| format!("OpenBook entry {title:?} is outside a bookpart"))?;
    let end = bookparts(source)
        .map(|(position, _)| position)
        .find(|&p| p > found[0])
        .unwrap_or(source.len());
    Ok(&source[preceding..end])
}

pub fn convert(source: &str, options: Options<'_>) -> Result<String> {
    check(
        matches!(options.measure_numbers, None | Some("system" | "none")),
        "Invalid measure numbering",
    )?;
    let source = if let Some(title) = options.title {
        select_title(source, title)?
    } else {
        source
    };
    check(
        bookparts(source).count() <= 1,
        "Choose one OpenBook entry with --title",
    )?;
    let tokens = syntax::lex(source)?;
    if let Some(xml) = ensemble::convert(&tokens, &options)? {
        return Ok(xml);
    }
    let mut score = lilypond::read_score(&tokens)?;
    let plan = score::plan(&mut score, options.page_breaks)?;
    musicxml::write(&score, &plan, options.title, options.measure_numbers)
}
