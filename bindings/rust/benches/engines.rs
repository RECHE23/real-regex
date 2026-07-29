//! Native criterion benchmark: the real-regex crate vs the regex crate, on the BENCHMARKS §E families. Both
//! sides run through their own Rust API in-process, so there is no FFI asymmetry for this pair (the C duel's
//! REAL-vs-rust rows pay a cross-process cost the duel documents; these rows are measured natively).
//!
//! Four groups per case, because the scan rows alone leave two blind spots that cost real time:
//!
//!   find/      whole-match spans over the 64 KiB corpus
//!   captures/  the same scan materialising every group
//!   compile/   `Regex::new` alone — the scan groups build the pattern OUTSIDE the timed closure
//!   first_use/ `Regex::new` plus one short search — structures built lazily on first use land here,
//!              and criterion's warm-up absorbs them entirely in the scan groups
//!
//! Those two blind spots are not hypothetical: `\b\w+\b` spent 105 us in a quadratic word-subset test
//! at compile, and `(\w+)@(\w+)` spent 21 ms building its one-pass table on first use. Both were
//! invisible to every row above, and both are now bounded by a row below.
use criterion::{black_box, criterion_group, criterion_main, BenchmarkId, Criterion, Throughput};

const CASES: &[(&str, &str)] = &[
    ("class", r"[a-z]+"),
    ("digits", r"[0-9]+"),
    ("fields", r"[^,]+"),
    ("email", r"(\w+)@(\w+)"),
    ("literal", r"dog"),
    ("alternation", r"fox|dog|cat"),
    ("word_bound", r"\b\w+\b"),
    ("no_match", r"\d{4}-\d{2}-\d{2}"),
    // A BOUNDED REPEAT. `\w+` and `\w{8}` look alike and are not: the counted form leaves the class-loop
    // fast path and runs on the general VM, and it grows superlinearly in the count -- 247 us for `\w+`,
    // 499 for `\w{4}`, 2369 for `\w{8}` over this corpus. Nothing above measured a counted repeat, so
    // none of that was visible. Found while adding the icase rows below, and not an icase effect at all:
    // `(?i)\w{8}` and `\w{8}` time identically.
    ("repeat", r"\w{8}"),
    // Case-insensitive families. The suite had none, and both compile-cost defects this project has
    // shipped lived in the icase path: `unicode_casefold` scanning the whole fold table per class, and
    // then folding the same class once per repetition of a bounded repeat. Neither was visible to any row
    // above -- the same argument the header makes for the compile/ and first_use/ groups, applied to the
    // flag rather than to the phase.
    //
    // Two shapes, because icase does different things to each: a literal folds to a small alternation of
    // cases and stays cheap; `[a-z]` gains the long s and the Kelvin sign, so it stops being a pure ASCII
    // class and emits as a code-point class -- which costs it 4.3x its plain form on the scan.
    ("icase_literal", r"(?i)dog"),
    ("icase_class", r"(?i)[a-z]+"),
];

// A deterministic ~64 KiB corpus of mixed words / digits / emails / commas — representative for every family
// (the no-match date pattern finds nothing in it, the intended prefilter case).
fn corpus() -> String {
    let unit = "the quick brown fox jumps over 12 lazy dogs, cat9 and root@localhost meet fox42, ";
    unit.repeat(64 * 1024 / unit.len())
}

// One corpus unit: long enough that every family finds something, short enough that the scan does not
// hide what the compile and first-use groups exist to measure.
const SHORT: &str = "the quick brown fox jumps over 12 lazy dogs, cat9 and root@localhost meet fox42, ";

fn bench(c: &mut Criterion) {
    let text = corpus();
    for &(name, pat) in CASES {
        let re = real_regex::Regex::new(pat).unwrap();
        let rx = regex::Regex::new(pat).unwrap();

        // find: whole-match spans (the span-0 fast path — allocates nothing per match).
        let mut find = c.benchmark_group(format!("find/{name}"));
        find.throughput(Throughput::Bytes(text.len() as u64));
        find.bench_with_input(BenchmarkId::new("real", name), &text, |b, t| {
            b.iter(|| black_box(re.find_iter(t).count()))
        });
        find.bench_with_input(BenchmarkId::new("regex", name), &text, |b, t| {
            b.iter(|| black_box(rx.find_iter(t).count()))
        });
        find.finish();

        // captures: materializes every group (the crate still allocates the group vector per match).
        let mut caps = c.benchmark_group(format!("captures/{name}"));
        caps.throughput(Throughput::Bytes(text.len() as u64));
        caps.bench_with_input(BenchmarkId::new("real", name), &text, |b, t| {
            b.iter(|| black_box(re.captures_iter(t).count()))
        });
        caps.bench_with_input(BenchmarkId::new("regex", name), &text, |b, t| {
            b.iter(|| black_box(rx.captures_iter(t).count()))
        });
        caps.finish();

        // compile: the pattern alone. No throughput — bytes of corpus mean nothing here.
        let mut comp = c.benchmark_group(format!("compile/{name}"));
        comp.bench_function(BenchmarkId::new("real", name), |b| {
            b.iter(|| black_box(real_regex::Regex::new(pat).unwrap()))
        });
        comp.bench_function(BenchmarkId::new("regex", name), |b| {
            b.iter(|| black_box(regex::Regex::new(pat).unwrap()))
        });
        comp.finish();

        // first_use: a FRESH regex per iteration plus one short search, so anything built lazily on the
        // first match attempt is paid inside the timed closure. Subtracting the compile row above gives
        // the lazy build on its own.
        let mut first = c.benchmark_group(format!("first_use/{name}"));
        first.bench_function(BenchmarkId::new("real", name), |b| {
            b.iter(|| {
                let r = real_regex::Regex::new(pat).unwrap();
                black_box(r.find(SHORT).is_some())
            })
        });
        first.bench_function(BenchmarkId::new("regex", name), |b| {
            b.iter(|| {
                let r = regex::Regex::new(pat).unwrap();
                black_box(r.find(SHORT).is_some())
            })
        });
        first.finish();
    }
}

criterion_group!(benches, bench);
criterion_main!(benches);
