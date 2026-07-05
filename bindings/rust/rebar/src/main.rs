//! A rebar engine runner for the real-regex crate. Speaks rebar's KLV protocol (see rebar's KLV.md): argv[1]
//! is the model (count / count-spans / count-captures / grep / grep-captures / compile), the benchmark is read
//! as KLV on stdin, and each timing sample is printed to stdout as "<nanoseconds>,<count>". `--version` prints
//! the crate version.
//!
//! real-regex is single-pattern and its str API needs a UTF-8 haystack; multi-pattern (RegexSet-style) and
//! non-UTF-8 benchmarks are reported as errors, which rebar records as "unsupported" for those cases.
use std::io::{Read, Write};
use std::time::{Duration, Instant};

#[derive(Default)]
struct Benchmark {
    model: String,
    patterns: Vec<String>,
    case_insensitive: bool,
    unicode: bool,
    haystack: Vec<u8>,
    max_iters: u64,
    max_warmup_iters: u64,
    max_time: Duration,
    max_warmup_time: Duration,
}

// Parse rebar's KLV: repeated "key:length:value\n", length being the byte count of value.
fn parse_klv(mut input: &[u8]) -> Benchmark {
    let mut b = Benchmark::default();
    while !input.is_empty() {
        let k = input.iter().position(|&c| c == b':').expect("KLV: missing key ':'");
        let key = String::from_utf8_lossy(&input[..k]).into_owned();
        input = &input[k + 1..];
        let l = input.iter().position(|&c| c == b':').expect("KLV: missing length ':'");
        let len: usize = std::str::from_utf8(&input[..l]).unwrap().parse().expect("KLV: bad length");
        input = &input[l + 1..];
        let value = &input[..len];
        input = &input[len..];
        if input.first() == Some(&b'\n') {
            input = &input[1..];
        }
        let s = || String::from_utf8_lossy(value).into_owned();
        let n = || std::str::from_utf8(value).unwrap().trim().parse::<u64>().unwrap_or(0);
        match key.as_str() {
            "model" => b.model = s(),
            "pattern" => b.patterns.push(s()),
            "case-insensitive" => b.case_insensitive = value == b"true",
            "unicode" => b.unicode = value == b"true",
            "haystack" => b.haystack = value.to_vec(),
            "max-iters" => b.max_iters = n(),
            "max-warmup-iters" => b.max_warmup_iters = n(),
            "max-time" => b.max_time = parse_duration(value),
            "max-warmup-time" => b.max_warmup_time = parse_duration(value),
            _ => {} // name, etc. — ignored
        }
    }
    b
}

// rebar durations look like "3s" / "1500ms" / "500us" / "1000ns", or a bare nanosecond integer.
fn parse_duration(value: &[u8]) -> Duration {
    let t = std::str::from_utf8(value).unwrap_or("").trim();
    for (suffix, unit) in [("ns", 1u64), ("us", 1_000), ("ms", 1_000_000), ("s", 1_000_000_000)] {
        if let Some(num) = t.strip_suffix(suffix) {
            if let Ok(v) = num.trim().parse::<f64>() {
                return Duration::from_nanos((v * unit as f64) as u64);
            }
        }
    }
    t.parse::<u64>().map(Duration::from_nanos).unwrap_or(Duration::from_secs(3))
}

// rebar's timer::run: a warm-up phase, then measured samples until max-iters or max-time.
fn run(bench: impl Fn() -> u64, b: &Benchmark) -> Vec<(Duration, u64)> {
    let w = Instant::now();
    for _ in 0..b.max_warmup_iters {
        if w.elapsed() >= b.max_warmup_time {
            break;
        }
        bench();
    }
    let mut samples = Vec::new();
    let m = Instant::now();
    for _ in 0..b.max_iters {
        if !samples.is_empty() && m.elapsed() >= b.max_time {
            break;
        }
        let t = Instant::now();
        let count = bench();
        samples.push((t.elapsed(), count));
    }
    samples
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.iter().any(|a| a == "--version") {
        println!("{}", real_regex::VERSION);
        return;
    }
    let model = args.get(1).cloned().unwrap_or_default();

    let mut input = Vec::new();
    std::io::stdin().read_to_end(&mut input).expect("read stdin");
    let b = parse_klv(&input);

    if b.patterns.len() != 1 {
        eprintln!("real-regex is single-pattern; got {} patterns", b.patterns.len());
        std::process::exit(1);
    }
    let haystack = match std::str::from_utf8(&b.haystack) {
        Ok(h) => h,
        Err(_) => {
            eprintln!("real-regex's str API needs a UTF-8 haystack");
            std::process::exit(1);
        }
    };
    let re = real_regex::RegexBuilder::new(&b.patterns[0])
        .case_insensitive(b.case_insensitive)
        .unicode(b.unicode)
        .build()
        .unwrap_or_else(|e| {
            eprintln!("compile error: {e}");
            std::process::exit(1);
        });

    let count_caps = |text: &str| -> u64 {
        let mut n = 0;
        for caps in re.captures_iter(text) {
            for i in 0..caps.len() {
                if caps.get(i).is_some() {
                    n += 1;
                }
            }
        }
        n
    };
    let samples = match model.as_str() {
        "count" => run(|| re.find_iter(haystack).count() as u64, &b),
        "count-spans" => run(|| re.find_iter(haystack).map(|m| m.len() as u64).sum(), &b),
        "count-captures" => run(|| count_caps(haystack), &b),
        "grep" => run(|| haystack.lines().filter(|l| re.is_match(l)).count() as u64, &b),
        "grep-captures" => run(|| haystack.lines().map(count_caps).sum(), &b),
        // compile: rebuild each iteration; the count verifies the compile produced a working regex.
        "compile" => run(
            || {
                real_regex::RegexBuilder::new(&b.patterns[0])
                    .case_insensitive(b.case_insensitive)
                    .unicode(b.unicode)
                    .build()
                    .map(|r| r.find_iter(haystack).count() as u64)
                    .unwrap_or(0)
            },
            &b,
        ),
        other => {
            eprintln!("unknown model: {other}");
            std::process::exit(1);
        }
    };

    let mut out = std::io::stdout();
    for (dur, count) in samples {
        writeln!(out, "{},{}", dur.as_nanos(), count).expect("write stdout");
    }
}
