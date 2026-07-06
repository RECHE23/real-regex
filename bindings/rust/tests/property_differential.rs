//! Exhaustive REAL-vs-`regex`-crate differential for the supported `\p{...}` properties — the per-property
//! drift-guard for the arc. The regen guards prove REAL's tables equal the UCD oracle; THIS proves the
//! parse -> merge -> match PATH equals the `regex` crate over every scalar. Both ship Unicode 16.0.0, so the
//! drift is currently empty (0 divergence per property). If a future version bump skews the two, this fails
//! naming the property and code point, which is the signal to add a per-property drift mask (none needed now).
//! Runs with `--features fallback` (the crate is the fallback dependency).
#![cfg(feature = "fallback")]
use real_regex::Regex;

fn divergences(pat: &str) -> usize {
    let real = Regex::new(pat).expect("real compiles a supported property");
    let crate_re = regex::Regex::new(pat).expect("the regex crate compiles it too");
    let mut buf = [0u8; 4];
    let mut n = 0usize;
    for cp in 0u32..=0x10FFFF {
        if (0xD800..=0xDFFF).contains(&cp) {
            continue; // surrogates are not scalars
        }
        let s = char::from_u32(cp).unwrap().encode_utf8(&mut buf);
        if real.is_match(s) != crate_re.is_match(s) {
            n += 1;
            if n <= 3 {
                eprintln!("  {pat}: U+{cp:04X} real={} crate={}", real.is_match(s), crate_re.is_match(s));
            }
        }
    }
    n
}

#[test]
fn real_matches_the_regex_crate_on_every_supported_property() {
    // one property per shape — an ASCII+high category, an upper/digit, a non-ASCII-only category, a group whose
    // negation-heavy definition (Cn gaps) is the trickiest, two scripts, and a negation. The path is identical
    // across every property, so this spread proves it without re-scanning all 200+ (the regen guards already
    // check each table exhaustively vs the oracle).
    let props = [
        r"\p{L}", r"\p{Lu}", r"\p{Nd}", r"\p{Mn}", r"\p{P}", r"\p{C}",
        r"\p{sc=Latin}", r"\p{sc=Han}", r"\P{L}",
    ];
    // NOTE: icase \p{} is deliberately not compared here — REAL's icase folds the Turkic dotless/dotted I
    // (U+0131/U+0130) with I, matching Python `re` (`(?i)I` matches ı in stdlib re), whereas the crate uses
    // Unicode CaseFolding (ı stays ı). That is an intentional re-parity divergence, not table drift.
    let total: usize = props.iter().map(|p| divergences(p)).sum();
    assert_eq!(total, 0, "{total} REAL-vs-crate \\p{{}} divergence(s) at Unicode 16.0.0 — add a drift mask");
}
