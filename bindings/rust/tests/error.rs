//! The typed Error and its structured classification (via the C ABI code, not the message text).
use real_regex::{Error, Regex};

#[test]
fn classifies_unsupported_vs_syntax() {
    assert!(matches!(Regex::new(r"\p{Alphabetic}+").unwrap_err(), Error::Unsupported { .. }));
    assert!(Regex::new(r"(\w+)\1").unwrap_err().is_unsupported()); // a backreference is unsupported, not syntax
    assert!(matches!(Regex::new(r"(a").unwrap_err(), Error::Syntax { .. }));
    if let Error::Unsupported { hint, .. } = Regex::new(r"\p{Alphabetic}+").unwrap_err() {
        assert!(hint.contains("fallback") && hint.contains("COMPATIBILITY"), "hint should sell the fix");
    }
}

#[test]
fn unicode_general_category_and_script_compile_natively() {
    // \p{Gc} and \p{sc=...} are built into REAL now — they compile without the fallback feature.
    assert!(Regex::new(r"\p{L}+").is_ok());
    assert!(Regex::new(r"\P{Nd}").is_ok());
    assert!(Regex::new(r"\p{sc=Greek}").is_ok());
    assert!(Regex::new(r"\pN").is_ok());
}
