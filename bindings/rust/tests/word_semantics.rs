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

#[test]
fn word_boundary_assertions_inherit_the_word_set() {
    // `\b` `\B` `\<` `\>` derive from the same word-set as `\w`, so they diverge on the same code points — a
    // second door the differential fuzzer found (via `\<`). U+05A2 (Hebrew accent, Mn) is a word char for UTS#18
    // (the `regex` crate) but not for CPython/REAL, so a word run breaks at it in REAL. In "a\u{05A2}b" (bytes:
    // a=0, U+05A2=1..3, b=3) REAL's `\<` (word-start) fires before 'a' AND 'b'; the `regex` crate, treating the
    // Mn as a word char, fires only before 'a'.
    let starts: Vec<_> = Regex::new(r"\<").unwrap().find_iter("a\u{05A2}b").map(|m| m.start()).collect();
    assert_eq!(starts, vec![0, 3], "REAL: word-start before 'a' and before 'b' (the Mn is not a word char)");
    assert!(!word('\u{05A2}'), "U+05A2 (Mn): not \\w in CPython/REAL — the root of the \\< divergence");
}
