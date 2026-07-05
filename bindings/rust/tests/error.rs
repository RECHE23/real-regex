//! The typed Error and its structured classification (via the C ABI code, not the message text).
use real_regex::{Error, Regex};

#[test]
fn classifies_unsupported_vs_syntax() {
    assert!(matches!(Regex::new(r"\p{L}+").unwrap_err(), Error::Unsupported { .. }));
    assert!(Regex::new(r"(\w+)\1").unwrap_err().is_unsupported()); // a backreference is unsupported, not syntax
    assert!(matches!(Regex::new(r"(a").unwrap_err(), Error::Syntax { .. }));
    if let Error::Unsupported { hint, .. } = Regex::new(r"\p{L}+").unwrap_err() {
        assert!(hint.contains("fallback") && hint.contains("COMPATIBILITY"), "hint should sell the fix");
    }
}
