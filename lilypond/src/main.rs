use lilypond_musicxml::{Options, Result, convert};
use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::ExitCode;

const USAGE: &str = "Usage: lilypond-musicxml <INPUT.ly> <OUTPUT.musicxml>\n\
    [--title TITLE] [--measure-numbers system|none] [--page-break-before MEASURE]...";

fn run() -> Result<()> {
    let mut input = None;
    let mut output = None;
    let mut title = None;
    let mut measure_numbers = None;
    let mut page_breaks = Vec::new();
    let mut args = env::args_os().skip(1);
    while let Some(arg) = args.next() {
        match arg.to_str() {
            Some("--help" | "-h") => {
                println!("{USAGE}");
                return Ok(());
            }
            Some("--title") => {
                title = Some(
                    args.next()
                        .and_then(|value| value.into_string().ok())
                        .ok_or("Missing --title value")?,
                )
            }
            Some("--measure-numbers") => {
                measure_numbers = Some(
                    args.next()
                        .and_then(|value| value.into_string().ok())
                        .ok_or("Missing --measure-numbers value")?,
                )
            }
            Some("--page-break-before") => {
                let number = args
                    .next()
                    .and_then(|s| s.into_string().ok())
                    .and_then(|s| s.parse::<i64>().ok())
                    .ok_or("Expected an integer after --page-break-before")?;
                page_breaks.push(number);
            }
            Some(value) if value.starts_with('-') => return Err(format!("Unknown option {value}")),
            _ => {
                if input.is_none() {
                    input = Some(PathBuf::from(arg));
                } else if output.is_none() {
                    output = Some(PathBuf::from(arg));
                } else {
                    return Err(USAGE.into());
                }
            }
        }
    }
    let input = input.ok_or(USAGE)?;
    let output = output.ok_or(USAGE)?;
    let source =
        fs::read_to_string(&input).map_err(|error| format!("{}: {error}", input.display()))?;
    let xml = convert(
        &source,
        Options {
            title: title.as_deref(),
            measure_numbers: measure_numbers.as_deref(),
            page_breaks: &page_breaks,
        },
    )
    .map_err(|error| format!("{}: {error}", input.display()))?;
    fs::write(&output, xml).map_err(|error| format!("{}: {error}", output.display()))
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
