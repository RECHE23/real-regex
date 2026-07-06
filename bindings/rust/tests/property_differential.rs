//! Exhaustive REAL-vs-`regex`-crate differential for the supported `\p{...}` properties — the per-property
//! drift-guard for the arc. The regen guards prove REAL's tables equal the UCD oracle; THIS proves the
//! parse -> merge -> match PATH equals the `regex` crate over every scalar. Both ship Unicode 16.0.0, so the
//! non-icase drift is empty (0 divergence per property). Under `(?i)` the two diverge on exactly two code
//! points — the Turkish dotted/dotless I (U+0130 İ, U+0131 ı) — which REAL folds with I/i (Python `re`'s
//! equivalence via upper/lower) and the crate keeps apart (Unicode simple CaseFolding); both are correct for
//! their contract (see the README divergence note), so those two are excluded from the icase comparison. Any
//! OTHER divergence fails, naming the property and code point — the signal to extend the mask.
//! Runs with `--features fallback` (the crate is the fallback dependency).
#![cfg(feature = "fallback")]
use real_regex::Regex;

/// The two code points where REAL's icase folding (Python `re`) and the crate's (Unicode CaseFolding) diverge.
/// Mirrors the fuzzer's ICASE_FOLD_DELTAS; pinned here so a third one appearing (e.g. a Unicode bump) fails loud.
const TURKISH_I: [char; 2] = ['\u{0130}', '\u{0131}'];

fn divergences(pat: &str, allowed: &[char]) -> usize {
    let real = Regex::new(pat).expect("real compiles a supported property");
    let crate_re = regex::Regex::new(pat).expect("the regex crate compiles it too");
    let mut buf = [0u8; 4];
    let mut n = 0usize;
    for cp in 0u32..=0x10FFFF {
        if (0xD800..=0xDFFF).contains(&cp) {
            continue; // surrogates are not scalars
        }
        let c = char::from_u32(cp).unwrap();
        let s = c.encode_utf8(&mut buf);
        if real.is_match(s) != crate_re.is_match(s) && !allowed.contains(&c) {
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
    let non_icase = [
        r"\p{L}", r"\p{Lu}", r"\p{Nd}", r"\p{Mn}", r"\p{P}", r"\p{C}",
        r"\p{sc=Latin}", r"\p{sc=Han}", r"\P{L}",
    ];
    let total: usize = non_icase.iter().map(|p| divergences(p, &[])).sum();
    assert_eq!(total, 0, "{total} REAL-vs-crate \\p{{}} divergence(s) at Unicode 16.0.0 — add a drift mask");

    // icase: identical to the crate everywhere except the two documented Turkish-I code points.
    let icase = [r"(?i)\p{Lu}", r"(?i)\p{Ll}", r"(?i)\P{Lu}", r"(?i)\p{sc=Greek}"];
    let itotal: usize = icase.iter().map(|p| divergences(p, &TURKISH_I)).sum();
    assert_eq!(itotal, 0, "{itotal} unexpected icase \\p{{}} divergence(s) beyond the Turkish-I deltas");
}
