//! Native criterion benchmark: the real-regex crate vs the regex crate, on the BENCHMARKS §E families. Both
//! sides run through their own Rust API in-process, so there is no FFI asymmetry for this pair (the C duel's
//! REAL-vs-rust rows pay a cross-process cost the duel documents; these rows are measured natively). Each
//! case times a full capture-extracting scan (captures_iter, the apples-to-apples work) over the same corpus.
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
];

// A deterministic ~64 KiB corpus of mixed words / digits / emails / commas — representative for every family
// (the no-match date pattern finds nothing in it, the intended prefilter case).
fn corpus() -> String {
    let unit = "the quick brown fox jumps over 12 lazy dogs, cat9 and root@localhost meet fox42, ";
    unit.repeat(64 * 1024 / unit.len())
}

fn bench(c: &mut Criterion) {
    let text = corpus();
    for &(name, pat) in CASES {
        let re = real_regex::Regex::new(pat).unwrap();
        let rx = regex::Regex::new(pat).unwrap();
        let mut group = c.benchmark_group(name);
        group.throughput(Throughput::Bytes(text.len() as u64));
        group.bench_with_input(BenchmarkId::new("real", name), &text, |b, t| {
            b.iter(|| black_box(re.captures_iter(t).count()))
        });
        group.bench_with_input(BenchmarkId::new("regex", name), &text, |b, t| {
            b.iter(|| black_box(rx.captures_iter(t).count()))
        });
        group.finish();
    }
}

criterion_group!(benches, bench);
criterion_main!(benches);
