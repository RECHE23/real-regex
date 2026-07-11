//! The typed Error and its structured classification (via the C ABI code, not the message text).
use real_regex::{Error, Regex};

#[test]
fn classifies_unsupported_vs_syntax() {
    // Bidi_Class: a real, well-known UCD enumerated property (like Script, but for bidi text) that REAL
    // does not implement -- General_Category, Script and the standard binary properties are (see
    // unicode_general_category_and_script_compile_natively / \p{Alphabetic} below, both native now).
    assert!(matches!(Regex::new(r"\p{Bidi_Class=L}+").unwrap_err(), Error::Unsupported { .. }));
    assert!(Regex::new(r"(\w+)\1").unwrap_err().is_unsupported()); // a backreference is unsupported, not syntax
    assert!(matches!(Regex::new(r"(a").unwrap_err(), Error::Syntax { .. }));
    if let Error::Unsupported { hint, .. } = Regex::new(r"\p{Bidi_Class=L}+").unwrap_err() {
        assert!(hint.contains("fallback") && hint.contains("COMPATIBILITY"), "hint should sell the fix");
    }
}

#[test]
fn unicode_general_category_and_script_compile_natively() {
    // \p{Gc}, \p{sc=...}/\p{scx=...} and the standard binary properties are built into REAL now — they
    // compile without the fallback feature. sc=/scx= accept both the long name and the short UAX24 code.
    assert!(Regex::new(r"\p{L}+").is_ok());
    assert!(Regex::new(r"\P{Nd}").is_ok());
    assert!(Regex::new(r"\p{sc=Greek}").is_ok());
    assert!(Regex::new(r"\p{sc=Grek}").is_ok());
    assert!(Regex::new(r"\p{scx=Grek}").is_ok());
    assert!(Regex::new(r"\pN").is_ok());
    assert!(Regex::new(r"\p{Alphabetic}+").is_ok());
    assert!(Regex::new(r"\P{White_Space}").is_ok());
}

#[test]
fn scx_is_a_superset_of_script() {
    // U+0300 COMBINING GRAVE ACCENT: Script=Inherited, but Script_Extensions includes Greek (and others)
    // -- \p{scx=Grek} matches it, bare \p{Grek} (== \p{sc=Grek}, the partition) does not.
    let combining_grave = "\u{0300}";
    assert!(!Regex::new(r"\p{Grek}").unwrap().is_match(combining_grave));
    assert!(!Regex::new(r"\p{sc=Grek}").unwrap().is_match(combining_grave));
    assert!(Regex::new(r"\p{scx=Grek}").unwrap().is_match(combining_grave));
}
