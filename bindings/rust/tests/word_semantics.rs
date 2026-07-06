//! REAL's `\w` follows Python `re` (its contract), not UTS#18 — pinned so the CPython-vs-UTS#18 divergence the
//! differential fuzzer surfaced stays asserted on REAL's side. See the README "Divergences" table and the
//! reproducible audit in `fuzz/unicode_probe/`. (`\d` and `\s` are identical to the `regex` crate — both
//! `\p{Nd}` and Unicode White_Space respectively — so only `\w` is pinned here.)
use real_regex::Regex;

fn word(c: char) -> bool {
    Regex::new(r"^\w$").unwrap().is_match(&c.to_string())
}

#[test]
fn word_class_follows_cpython_not_uts18() {
    // UTS#18 (the `regex` crate) matches these; CPython `re` — and so REAL — does not:
    assert!(!word('\u{0325}'), "U+0325 combining ring below (Mn): not \\w in CPython");
    assert!(!word('\u{200D}'), "U+200D ZWJ (Join_Control): not \\w in CPython");
    assert!(!word('\u{2040}'), "U+2040 character tie (Pc beyond _): not \\w in CPython");

    // CPython `re` — and so REAL — matches these; UTS#18 (the `regex` crate) does not:
    assert!(word('\u{00B2}'), "U+00B2 superscript two (No): \\w in CPython (str.isalnum)");
    assert!(word('\u{2153}'), "U+2153 vulgar fraction one third (No): \\w in CPython");

    // Both agree — the shared core of `\w`:
    assert!(word('_'), "underscore");
    assert!(word('a'), "ascii letter");
    assert!(word('\u{00E9}'), "e-acute (Ll)");
    assert!(word('\u{0660}'), "U+0660 arabic-indic digit zero (Nd)");
}
