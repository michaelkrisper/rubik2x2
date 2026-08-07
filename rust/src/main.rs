//! Command line interface.
//!
//! Hand-rolled argument parsing: the grammar in `SPEC.md` is four subcommands
//! and five flags, and a dependency-free build is part of the exercise.

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use rubik2x2::cube::{self, State};
use rubik2x2::rng::Rng;
use rubik2x2::table::DistanceTable;
use rubik2x2::{HARDEST, bench, bfs, ida, memory};

/// Counting allocations is how `bench` answers `peak_mb`; see [`memory`].
#[global_allocator]
static ALLOCATOR: memory::Tracking = memory::Tracking;

const USAGE: &str = "\
rubik2x2 — optimal solver for the 2x2x2 Rubik's cube

usage:
  rubik2x2 solve <state> [--algorithm bfs|ida|table]
  rubik2x2 scramble [-n N] [--seed S]
  rubik2x2 table
  rubik2x2 bench [--json] [--scrambles FILE] [-n N]

A state is 24 colour letters (rwbypg) in side order UP FRONT DOWN BACK LEFT
RIGHT, each face read row-major; '-' reads it from stdin instead.
";

/// Where the shared scramble list lives, tried in order when `--scrambles` is
/// not given. Every language in the repository benchmarks the same 200 states.
const DEFAULT_SCRAMBLES: [&str; 3] =
    ["bench/scrambles.txt", "../bench/scrambles.txt", "../../bench/scrambles.txt"];

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    match run(&arguments) {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("{message}");
            ExitCode::from(2)
        }
    }
}

fn run(arguments: &[String]) -> Result<(), String> {
    let Some(command) = arguments.first() else {
        print!("{USAGE}");
        return Ok(());
    };
    let rest = &arguments[1..];

    match command.as_str() {
        "solve" => solve(rest),
        "scramble" => scramble(rest),
        "table" => table(rest),
        "bench" => run_bench(rest),
        "-h" | "--help" | "help" => {
            print!("{USAGE}");
            Ok(())
        }
        other => Err(format!("unknown command {other:?}\n\n{USAGE}")),
    }
}

/// `solve <state> [--algorithm bfs|ida|table]` — prints the moves and nothing
/// else, so it pipes.
fn solve(arguments: &[String]) -> Result<(), String> {
    let mut algorithm = "bfs".to_string();
    let mut text: Option<String> = None;

    let mut arguments = arguments.iter();
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--algorithm" | "-a" => {
                algorithm = arguments.next().ok_or("--algorithm needs a value")?.clone();
            }
            other if other.starts_with("--algorithm=") => {
                algorithm = other["--algorithm=".len()..].to_string();
            }
            other if text.is_none() => text = Some(other.to_string()),
            other => return Err(format!("unexpected argument {other:?}")),
        }
    }

    let text = match text.as_deref() {
        None => return Err("solve needs a state\n".to_string() + USAGE),
        Some("-") => std::io::read_to_string(std::io::stdin()).map_err(|e| e.to_string())?,
        Some(given) => given.to_string(),
    };
    let state = cube::parse(&text).map_err(|error| format!("invalid cube: {error}"))?;

    let solution = match algorithm.as_str() {
        "bfs" => bfs::solve(&state),
        "ida" => ida::solve(&state),
        "table" => DistanceTable::build().solve(&state),
        other => return Err(format!("unknown algorithm {other:?} (bfs, ida, table)")),
    }
    .map_err(|error| format!("no solution: {error}"))?;

    println!("{solution}");
    Ok(())
}

/// `scramble [-n N] [--seed S]` — prints a state string.
fn scramble(arguments: &[String]) -> Result<(), String> {
    let mut turns = 8usize;
    let mut seed: Option<u64> = None;

    let mut arguments = arguments.iter();
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "-n" | "--moves" => turns = parse_value(arguments.next(), "-n")?,
            "--seed" => seed = Some(parse_value(arguments.next(), "--seed")?),
            other => return Err(format!("unexpected argument {other:?}")),
        }
    }

    let mut rng = seed.map_or_else(Rng::from_clock, Rng::new);
    println!("{}", cube::to_string(&cube::scramble(&cube::SOLVED, turns, &mut rng)));
    Ok(())
}

/// `table` — builds the distance table and prints its histogram.
fn table(arguments: &[String]) -> Result<(), String> {
    if let Some(unexpected) = arguments.first() {
        return Err(format!("unexpected argument {unexpected:?}"));
    }

    let started = std::time::Instant::now();
    let table = DistanceTable::build();
    let elapsed = started.elapsed();

    // The numbers themselves go to stdout so they pipe; the commentary does not.
    eprintln!(
        "built in {:.0} ms, {:.1} MB resident — states at each quarter-turn distance:",
        elapsed.as_secs_f64() * 1000.0,
        table.size_bytes() as f64 / 1e6
    );
    let histogram: Vec<String> = table.histogram().iter().map(u32::to_string).collect();
    println!("{}", histogram.join(", "));
    eprintln!("this is OEIS A079762, the published distance distribution.");
    Ok(())
}

/// `bench [--json] [--scrambles FILE] [-n N]`.
fn run_bench(arguments: &[String]) -> Result<(), String> {
    let mut json = false;
    let mut path: Option<PathBuf> = None;
    let mut count: Option<usize> = None;

    let mut arguments = arguments.iter();
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--json" => json = true,
            "--scrambles" => {
                path = Some(PathBuf::from(arguments.next().ok_or("--scrambles needs a path")?));
            }
            "-n" | "--samples" => count = Some(parse_value(arguments.next(), "-n")?),
            other => return Err(format!("unexpected argument {other:?}")),
        }
    }

    let mut states = match path.or_else(default_scrambles) {
        Some(path) => read_scrambles(&path)?,
        None => generate_scrambles(count.unwrap_or(200)),
    };
    if let Some(count) = count {
        states.truncate(count);
    }
    if states.is_empty() {
        return Err("no scrambles to benchmark".to_string());
    }

    let hardest = cube::parse(HARDEST).expect("the antipodal position is a valid state");
    let report = bench::run(&states, &hardest, bench::default_workers());
    if json {
        println!("{}", report.to_json());
    } else {
        print!("{}", report.to_text(states.len()));
    }
    Ok(())
}

fn default_scrambles() -> Option<PathBuf> {
    DEFAULT_SCRAMBLES.iter().map(PathBuf::from).find(|path| path.exists())
}

fn read_scrambles(path: &Path) -> Result<Vec<State>, String> {
    let text = std::fs::read_to_string(path)
        .map_err(|error| format!("cannot read {}: {error}", path.display()))?;
    text.lines()
        .filter(|line| !line.trim().is_empty())
        .map(|line| cube::parse(line).map_err(|error| format!("{}: {error}", path.display())))
        .collect()
}

/// Fallback when no scramble file is around: reproducible random states.
fn generate_scrambles(count: usize) -> Vec<State> {
    (0..count).map(|seed| cube::scramble(&cube::SOLVED, 14, &mut Rng::new(seed as u64))).collect()
}

fn parse_value<T: std::str::FromStr>(value: Option<&String>, flag: &str) -> Result<T, String> {
    value
        .ok_or_else(|| format!("{flag} needs a value"))?
        .parse()
        .map_err(|_| format!("{flag} needs a number"))
}
