//! Pins the possessive-quantifier interpretation divergence (README "Divergences").
//!
//! REAL reads a `+` right after a quantifier as POSSESSIVE — match maximally, never give
//! back — the Python `re` 3.11+/PCRE2 grammar (REAL and `re` agree on the whole family).
//! The `regex` crate has no possessives and reads the same text as nested repetition
//! (`x?+` == `(?:x?)+`). Both compile, spans legitimately differ; the differential fuzzer
//! masks the class by form (`has_possessive_quantifier`). These pins are the deterministic
//! witnesses: if either engine changes its reading, they fail and the mask must be revisited.

fn real_spans(pat: &str, text: &str) -> Vec<(usize, usize)> {
    let re = real_regex::Regex::new(pat).unwrap();
    re.find_iter(text).map(|m| (m.start(), m.end())).collect()
}

fn crate_spans(pat: &str, text: &str) -> Vec<(usize, usize)> {
    let re = regex::Regex::new(pat).unwrap();
    re.find_iter(text).map(|m| (m.start(), m.end())).collect()
}

#[test]
fn optional_possessive_one_char_per_match() {
    // Possessive `a?+` takes its one optional 'a' and never gives it back; the crate's
    // `(?:a?)+` consumes the whole run in a single greedy match.
    assert_eq!(real_spans("a?+", "aaaa"), [(0, 1), (1, 2), (2, 3), (3, 4)]);
    assert_eq!(crate_spans("a?+", "aaaa"), [(0, 4)]);
}

#[test]
fn star_possessive_same_shape() {
    assert_eq!(real_spans("a*+", "aaaa"), [(0, 4)]);
    assert_eq!(crate_spans("a*+", "aaaa"), [(0, 4)]); // shapes agree here; the family diverges elsewhere
}

#[test]
fn brace_possessive_chunks_vs_single_run() {
    // Possessive `a{1,2}+` bites off two at a time; the crate's `(?:a{1,2})+` takes the run whole.
    assert_eq!(real_spans("a{1,2}+", "aaaa"), [(0, 2), (2, 4)]);
    assert_eq!(crate_spans("a{1,2}+", "aaaa"), [(0, 4)]);
}

#[test]
fn plus_possessive_never_gives_back() {
    // `a++a`: possessive `a++` swallows every 'a' and cannot give one back for the trailing
    // literal — no match anywhere. The crate's `(?:a+)+a` backtracks and matches the whole text.
    assert_eq!(real_spans("a++a", "aaaa"), []);
    assert_eq!(crate_spans("a++a", "aaaa"), [(0, 4)]);
}

#[test]
fn fuzz_witness_nul_optional_possessive() {
    // The differential-fuzz witness that surfaced the class: `\0?+` over a NUL run.
    assert_eq!(
        real_spans("\0?+", "\0\0\0\0"),
        [(0, 1), (1, 2), (2, 3), (3, 4)]
    );
    assert_eq!(crate_spans("\0?+", "\0\0\0\0"), [(0, 4)]);
}

#[test]
fn lazy_and_plain_quantifiers_agree() {
    // The non-possessive neighbours stay differential-clean: nothing to mask.
    for pat in ["a+", "a+?", "a*?", "a??"] {
        assert_eq!(real_spans(pat, "aaaa"), crate_spans(pat, "aaaa"), "{pat}");
    }
}
