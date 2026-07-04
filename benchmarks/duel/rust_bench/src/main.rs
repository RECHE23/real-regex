// REAL-vs-rust duel shim: time the `regex` crate's find_iter over a corpus, non-anchored, one pattern per
// invocation. Args: <pattern> <text-file>. Prints "<ns_per_byte> <match_count>". Best of N batches
// (median-ish via min, matching the C++ side's methodology). The crate version is pinned in Cargo.lock.
use std::time::Instant;
use regex::bytes::Regex;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let pat = &args[1];
    let text = std::fs::read(&args[2]).unwrap();
    let re = match Regex::new(pat) {
        Ok(r) => r,
        Err(_) => { println!("unsupported 0"); return; }
    };
    // warmup
    let mut cnt = 0usize;
    for _ in 0..3 { cnt = re.find_iter(&text).count(); }
    let mut best = f64::MAX;
    for _ in 0..15 {
        let t0 = Instant::now();
        let c = re.find_iter(&text).count();
        let dt = t0.elapsed().as_nanos() as f64;
        std::hint::black_box(c);
        if dt < best { best = dt; }
    }
    println!("{:.4} {}", best / text.len() as f64, cnt);
}
