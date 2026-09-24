// REAL-vs-rust duel shim: time the `regex` crate over a corpus, non-anchored, one pattern per invocation.
// Args: <pattern> <text-file> [mode].  mode = "find" (default) times find_iter (spans only, no capture
// extraction); mode = "captures" times captures_iter and touches every group (the apples-to-apples with
// REAL, whose find_iter always builds the full Match). Prints "<ns_per_byte> <match_count> <span_digest>":
// the digest folds every match's (start, end) with 64-bit FNV-1a exactly as real_bench.cpp does, over one
// untimed pass through the same iterator the timing used, so the duel compares answers, not just counts.
// Best of N batches (min, matching the C++ side). The crate version is pinned in Cargo.lock.
use std::time::Instant;
use regex::bytes::Regex;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let pat = &args[1];
    let text = std::fs::read(&args[2]).unwrap();
    let captures = args.get(3).map(|s| s == "captures").unwrap_or(false);
    let re = match Regex::new(pat) {
        Ok(r) => r,
        Err(_) => { println!("unsupported 0"); return; }
    };
    // Touch every group span so the capture engine actually runs (not just the span search).
    let run = |re: &Regex, text: &[u8]| -> usize {
        if captures {
            let mut acc = 0usize;
            for c in re.captures_iter(text) {
                for i in 0..c.len() {
                    if let Some(m) = c.get(i) { acc = acc.wrapping_add(m.end() - m.start()); }
                }
            }
            acc
        } else {
            re.find_iter(text).count()
        }
    };
    for _ in 0..3 { std::hint::black_box(run(&re, &text)); }
    let mut best = f64::MAX;
    for _ in 0..15 {
        let t0 = Instant::now();
        let c = run(&re, &text);
        let dt = t0.elapsed().as_nanos() as f64;
        std::hint::black_box(c);
        if dt < best { best = dt; }
    }
    let mut cnt = 0usize;
    let mut digest: u64 = 0xcbf2_9ce4_8422_2325;
    let mut fold = |start: usize, end: usize| {
        cnt += 1;
        for v in [start as u64, end as u64] {
            digest ^= v;
            digest = digest.wrapping_mul(0x0000_0100_0000_01b3);
        }
    };
    if captures {
        for c in re.captures_iter(&text) {
            let m = c.get(0).unwrap();
            fold(m.start(), m.end());
        }
    } else {
        for m in re.find_iter(&text) {
            fold(m.start(), m.end());
        }
    }
    println!("{:.4} {} {:016x}", best / text.len() as f64, cnt, digest);
}
