//! The fallback hint is printed only where both halves hold: the call site has a fallback, and
//! the delegate can run the pattern. The regex crate is linear too — it refuses a backreference
//! exactly as REAL does (see fallback.rs), so offering the feature there is a dead end.
use real_regex::{Error, Regex, RegexSet};

fn hint(e: Error) -> String {
    match e {
        Error::Unsupported { hint, .. } => hint,
        other => panic!("expected Unsupported, got {other:?}"),
    }
}

#[test]
fn regexset_never_advertises_a_fallback_it_does_not_have() {
    let h = hint(RegexSet::new([r"(\w+)\1"]).err().unwrap());
    assert!(h.contains("RegexSet never delegates"), "{h}");
    assert!(!h.contains("RegexBuilder::fallback"), "{h}");
}

#[cfg(feature = "fallback")]
#[test]
fn a_pattern_the_delegate_refuses_is_not_sold_the_feature() {
    // fallback.rs pins that this build cannot run it either.
    let h = hint(Regex::new(r"(\w+)\1").err().unwrap());
    assert!(h.contains("does not help here"), "{h}");
    assert!(!h.contains("RegexBuilder::fallback"), "{h}");
}

#[cfg(feature = "fallback")]
#[test]
fn a_pattern_the_delegate_accepts_names_the_switch() {
    let h = hint(Regex::new(r"\p{Word_Break=ALetter}+").err().unwrap());
    assert!(h.contains("RegexBuilder::fallback(true)"), "{h}");
}

#[cfg(not(feature = "fallback"))]
#[test]
fn without_the_feature_the_hint_promises_nothing_it_cannot_check() {
    // No regex crate linked, so no oracle: the hint may not assert either way.
    let h = hint(Regex::new(r"(\w+)\1").err().unwrap());
    assert!(h.contains("but not"), "{h}");
    assert!(!h.contains("RegexBuilder::fallback(true)"), "{h}");
}
