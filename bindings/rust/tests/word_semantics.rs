//! REAL's `\w` and `\s` follow Python `re` (its contract), not UTS#18 — pinned so the CPython-vs-UTS#18
//! divergences the differential fuzzer surfaced stay asserted on REAL's side. See the README "Divergences"
//! table and the reproducible audit in `fuzz/unicode_probe/`. (`\d` is identical to the `regex` crate — both
//! `\p{Nd}` — so it is not pinned here.)
use real_regex::Regex;

fn word(c: char) -> bool {
    Regex::new(r"^\w$").unwrap().is_match(&c.to_string())
}

fn space(c: char) -> bool {
    Regex::new(r"^\s$").unwrap().is_match(&c.to_string())
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

#[test]
fn space_class_follows_cpython_not_uts18() {
    // CPython `re` — and so REAL — matches these four; UTS#18 (the `regex` crate, Unicode White_Space) does not.
    // This is the divergence the differential fuzzer's control-char coverage now exercises, and the regression
    // the ASCII `\s` set once carried (it dropped them).
    for cp in 0x1Cu32..=0x1F {
        let c = char::from_u32(cp).unwrap();
        assert!(space(c), "U+{cp:04X} (FS/GS/RS/US): \\s in CPython (str.isspace)");
    }
    // Shared whitespace — matched by both:
    assert!(space(' '));
    assert!(space('\t'));
    assert!(space('\n'));
    assert!(space('\u{00A0}'), "NBSP (Zs)");
    // Not whitespace in either:
    assert!(!space('a'));
    assert!(!space('_'));
}
