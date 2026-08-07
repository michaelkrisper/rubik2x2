//! Compare the solvers, setup cost included.
//!
//! Per-solve time alone flatters the lookup table, because it hides the
//! exhaustive search that produced it. What actually decides which algorithm to
//! reach for is total time for the workload you have — so this reports setup,
//! per-solve and the break-even point where paying for the table starts to win.
//!
//! The protocol is fixed by `SPEC.md` so that every language in this repository
//! measures the same work: the same 200 scrambles from `bench/scrambles.txt`,
//! the same antipodal worst case, the same four rows of output.

use std::fmt::Write as _;
use std::hint::black_box;
use std::time::Instant;

use crate::cube::{State, apply_moves, is_solved};
use crate::table::DistanceTable;
use crate::{Result, coords, ida, memory, table};

/// What one algorithm cost on one workload.
#[derive(Debug, Clone)]
pub struct AlgorithmResult {
    pub name: &'static str,
    pub setup_ms: f64,
    pub avg_ms: f64,
    pub worst_ms: f64,
    pub peak_mb: f64,
    pub optimal: bool,
    /// Only the parallel table build reports this.
    pub workers: Option<usize>,
}

impl AlgorithmResult {
    /// Setup plus `solves` solves — the number that actually decides.
    pub fn total_ms(&self, solves: u32) -> f64 {
        self.setup_ms + f64::from(solves) * self.avg_ms
    }
}

/// One benchmark run, in the shape `SPEC.md` fixes for `--json`.
#[derive(Debug, Clone)]
pub struct Report {
    pub language: &'static str,
    pub version: String,
    pub build: String,
    pub algorithms: Vec<AlgorithmResult>,
}

/// Repeat a measurement until it has run for at least this long, so that a
/// solve measured in microseconds still gets a meaningful clock reading.
const MIN_SAMPLE_MS: f64 = 250.0;
/// Setup is measured a few times and the best run kept — it is a single event
/// per run, so the minimum is the least noisy estimate of its true cost.
const SETUP_REPEATS: u32 = 3;

fn ms(instant: Instant) -> f64 {
    instant.elapsed().as_secs_f64() * 1000.0
}

/// Milliseconds per repetition of `work`, repeated until [`MIN_SAMPLE_MS`].
fn measure(mut work: impl FnMut()) -> f64 {
    let started = Instant::now();
    let mut repeats = 0u32;
    loop {
        work();
        repeats += 1;
        let elapsed = ms(started);
        if elapsed >= MIN_SAMPLE_MS {
            return elapsed / f64::from(repeats);
        }
    }
}

/// Time one algorithm end to end, setup included.
///
/// `peak_mb` is measured the way `tracemalloc` would: the allocator's
/// high-water mark from the moment setup finished, minus what was already live
/// before it — so whatever the setup *keeps* (3.7 MB for the distance table)
/// counts, and everything the harness itself holds does not.
///
/// `distances` holds the true distance of each state, so that optimality can be
/// checked without a distance table sitting in the cache while another
/// algorithm is being timed. That detail is worth two hundred milliseconds: a
/// second 3.7 MB table resident during the build pushes it out of L3 and the
/// build measures nearly twice as slow.
fn evaluate<T>(
    name: &'static str,
    setup_repeats: u32,
    mut setup: impl FnMut() -> T,
    solve: impl Fn(&T, &State) -> Result<String>,
    states: &[State],
    hardest: &State,
    distances: &[u8],
) -> AlgorithmResult {
    let base = memory::live();

    let mut setup_ms = f64::INFINITY;
    let mut context = None;
    for _ in 0..setup_repeats.max(1) {
        drop(context.take()); // release the previous build before timing the next
        let started = Instant::now();
        let built = setup();
        setup_ms = setup_ms.min(ms(started));
        context = Some(built);
    }
    let context = context.expect("at least one setup run");

    memory::reset_peak();
    let worst_ms = measure(|| {
        black_box(solve(&context, hardest).expect("the antipodal position is solvable"));
    });
    let peak_mb = memory::peak().saturating_sub(base) as f64 / 1e6;

    let avg_ms = measure(|| {
        for state in states {
            black_box(solve(&context, state).expect("scrambles are solvable"));
        }
    }) / states.len() as f64;

    let optimal = states.iter().chain([hardest]).zip(distances).all(|(state, &distance)| {
        let solution = solve(&context, state).unwrap_or_default();
        solution.len() as u8 == distance
            && apply_moves(state, &solution).is_ok_and(|solved| is_solved(&solved))
    });

    AlgorithmResult {
        name,
        setup_ms: if setup_repeats == 0 { 0.0 } else { setup_ms },
        avg_ms,
        worst_ms,
        peak_mb,
        optimal,
        workers: None,
    }
}

/// Run all four rows: `bfs`, `ida`, `table` and the parallel table build.
pub fn run(states: &[State], hardest: &State, workers: usize) -> Report {
    // The move tables are shared by every algorithm and are built once, before
    // anything is timed — `SPEC.md` puts bfs's setup at zero, and this is what
    // makes that true rather than a rounding error.
    coords::tables();

    // The true distances, kept as 201 bytes rather than as the 3.7 MB table
    // they came from — see [`evaluate`]. Both tables are also compared here,
    // untimed, so that the parallel build's per-solve figures may be reused.
    let (distances, identical) = {
        let reference = DistanceTable::build();
        let distances: Vec<u8> = states
            .iter()
            .chain([hardest])
            .map(|state| coords::encode(state).map_or(u8::MAX, |index| reference.distance(index)))
            .collect();
        (distances, DistanceTable::build_parallel(workers) == reference)
    };

    let bfs = evaluate(
        "bfs",
        0,
        || (),
        |(), state| crate::bfs::solve(state),
        states,
        hardest,
        &distances,
    );
    let ida = evaluate(
        "ida",
        SETUP_REPEATS,
        ida::PatternDatabases::build,
        |databases, state| databases.solve(state),
        states,
        hardest,
        &distances,
    );
    let full = evaluate(
        "table",
        SETUP_REPEATS,
        DistanceTable::build,
        |table, state| table.solve(state),
        states,
        hardest,
        &distances,
    );

    // Only the build is parallelised, so the per-solve figures are by
    // construction the ones already measured — and the two tables were checked
    // byte-identical above before they are reused.
    let mut parallel =
        AlgorithmResult { name: "table-par", workers: Some(workers), ..full.clone() };
    parallel.optimal &= identical;
    let mut setup_ms = f64::INFINITY;
    for _ in 0..SETUP_REPEATS {
        let started = Instant::now();
        let built = DistanceTable::build_parallel(workers);
        setup_ms = setup_ms.min(ms(started));
        drop(built); // nothing else may be resident while the next build is timed
    }
    parallel.setup_ms = setup_ms;

    Report {
        language: "Rust",
        version: env!("RUSTC_VERSION").to_string(),
        build: format!(
            "rustc {}, {}, lto=fat, codegen-units=1",
            env!("RUSTC_VERSION"),
            env!("BUILD_PROFILE")
        ),
        algorithms: vec![bfs, ida, full, parallel],
    }
}

impl Report {
    /// Exactly one JSON object, in the schema `SPEC.md` fixes.
    pub fn to_json(&self) -> String {
        let algorithms: Vec<String> = self
            .algorithms
            .iter()
            .map(|result| {
                let workers = result
                    .workers
                    .map(|count| format!(", \"workers\": {count}"))
                    .unwrap_or_default();
                format!(
                    "    {{\"name\": \"{}\", \"setup_ms\": {:.1}, \"avg_ms\": {:.4}, \
                     \"worst_ms\": {:.4}, \"peak_mb\": {:.1}, \"optimal\": {}{}}}",
                    result.name,
                    result.setup_ms,
                    result.avg_ms,
                    result.worst_ms,
                    result.peak_mb,
                    result.optimal,
                    workers,
                )
            })
            .collect();

        let mut json = String::new();
        writeln!(json, "{{").unwrap();
        writeln!(json, "  \"language\": \"{}\",", escape(self.language)).unwrap();
        writeln!(json, "  \"version\": \"{}\",", escape(&self.version)).unwrap();
        writeln!(json, "  \"build\": \"{}\",", escape(&self.build)).unwrap();
        writeln!(json, "  \"algorithms\": [").unwrap();
        writeln!(json, "{}", algorithms.join(",\n")).unwrap();
        writeln!(json, "  ]").unwrap();
        write!(json, "}}").unwrap();
        json
    }

    /// The same numbers, laid out for a human.
    pub fn to_text(&self, solves: usize) -> String {
        let mut out = String::new();
        writeln!(out, "rubik2x2 benchmark — {solves} scrambles plus one antipodal position")
            .unwrap();
        writeln!(out, "{} reachable states, maximum distance 14 quarter turns", coords::N_STATES)
            .unwrap();
        writeln!(out, "{} — {}\n", self.language, self.build).unwrap();

        let header = format!(
            "{:<10}{:>11}{:>10}{:>11}{:>10}{:>9}",
            "algorithm", "setup ms", "avg ms", "worst ms", "peak MB", "optimal"
        );
        writeln!(out, "{header}").unwrap();
        writeln!(out, "{}", "-".repeat(header.len())).unwrap();
        for result in &self.algorithms {
            writeln!(
                out,
                "{:<10}{:>11.1}{:>10.4}{:>11.4}{:>10.1}{:>9}",
                result.name,
                result.setup_ms,
                result.avg_ms,
                result.worst_ms,
                result.peak_mb,
                if result.optimal { "yes" } else { "NO" }
            )
            .unwrap();
        }

        writeln!(out, "\ntotal time for a workload (setup + solves), seconds").unwrap();
        let counts = [1u32, 10, 100, 1_000, 10_000];
        write!(out, "{:<10}", "algorithm").unwrap();
        for count in counts {
            write!(out, "{count:>12}").unwrap();
        }
        writeln!(out).unwrap();
        writeln!(out, "{}", "-".repeat(10 + 12 * counts.len())).unwrap();
        for result in &self.algorithms {
            write!(out, "{:<10}", result.name).unwrap();
            for count in counts {
                write!(out, "{:>12.2}", result.total_ms(count) / 1000.0).unwrap();
            }
            writeln!(out).unwrap();
        }

        if let Some(point) = self.break_even("table", "bfs") {
            writeln!(
                out,
                "\nThe table overtakes bidirectional search at about {point} solves.\n\
                 Below that, building it costs more than it saves; above it, nothing\n\
                 else comes close."
            )
            .unwrap();
        }
        let (permutation, orientation) = ida::databases().reach();
        writeln!(
            out,
            "IDA*'s pattern databases reach only distance {} of 14, too loose to prune\n\
             a puzzle this small.",
            permutation.max(orientation)
        )
        .unwrap();
        out
    }

    /// Solves after which `fast` overtakes `slow`, setup included.
    pub fn break_even(&self, fast: &str, slow: &str) -> Option<u64> {
        let find = |name: &str| self.algorithms.iter().find(|result| result.name == name);
        let (fast, slow) = (find(fast)?, find(slow)?);
        let gain = slow.avg_ms - fast.avg_ms;
        (gain > 0.0).then(|| ((fast.setup_ms - slow.setup_ms) / gain) as u64 + 1)
    }
}

fn escape(text: &str) -> String {
    text.chars()
        .flat_map(|character| match character {
            '"' => vec!['\\', '"'],
            '\\' => vec!['\\', '\\'],
            other => vec![other],
        })
        .collect()
}

/// The default worker count for the parallel table build.
pub fn default_workers() -> usize {
    table::workers()
}
