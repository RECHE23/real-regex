//! RD.2b: the fallback feature. A pattern REAL cannot run linearly is delegated to the regex crate (opt-in,
//! per pattern), through our own types; the results must equal bare regex. Runs only with --features fallback.
#![cfg(feature = "fallback")]
use real_regex::{Engine, Regex, RegexBuilder};

fn fb(pat: &str) -> Regex {
    RegexBuilder::new(pat).fallback(true).build().unwrap()
}

#[test]
fn unicode_property_delegates_and_is_observable() {
    // Word_Break (a UAX#29 text-segmentation property) is not General_Category, Script or one of the
    // standard binary properties REAL now tabulates (\p{Alphabetic} moved natively in this train — see
    // unicode_binary_properties_run_natively_not_delegated below), so it is still delegated. `=` gives it
    // an explicit namespace REAL does not recognize (gc=/sc= only), same rejection path as an unknown one.
    let re = fb(r"\p{Word_Break=ALetter}+");
    assert_eq!(re.engine(), Engine::Fallback, "\\p{{Word_Break=ALetter}} must be delegated");
    let std = regex::Regex::new(r"\p{Word_Break=ALetter}+").unwrap();
    let text = "héllo, wörld 42";
    assert_eq!(re.find_iter(text).map(|m| (m.start(), m.end())).collect::<Vec<_>>(),
               std.find_iter(text).map(|m| (m.start(), m.end())).collect::<Vec<_>>(), "find_iter");
    assert_eq!(re.replace_all(text, "#"), std.replace_all(text, "#"), "replace_all");
    assert_eq!(re.split(text).collect::<Vec<_>>(), std.split(text).collect::<Vec<_>>(), "split");
}

#[test]
fn eligible_pattern_stays_on_real_even_with_fallback_requested() {
    let re = RegexBuilder::new(r"(\w+)@(\w+)").fallback(true).build().unwrap();
    assert_eq!(re.engine(), Engine::Real, "an eligible pattern must not be delegated");
}

#[test]
fn unicode_gc_and_script_run_natively_not_delegated() {
    // \p{Gc} and \p{sc=...} are built into REAL now — even with fallback requested they stay on REAL.
    for pat in [r"\p{L}+", r"\p{sc=Greek}+", r"\pN"] {
        let re = RegexBuilder::new(pat).fallback(true).build().unwrap();
        assert_eq!(re.engine(), Engine::Real, "{pat} must run on REAL, not fall back");
    }
}

#[test]
fn unicode_binary_properties_run_natively_not_delegated() {
    // The standard binary properties (\p{Alphabetic}, \p{White_Space}, ...) are built into REAL now too —
    // even with fallback requested they stay on REAL. This is why unicode_property_delegates_and_is_
    // observable above had to switch its delegated example to a Word_Break property.
    for pat in [r"\p{Alphabetic}+", r"\P{White_Space}", r"\p{Emoji}"] {
        let re = RegexBuilder::new(pat).fallback(true).build().unwrap();
        assert_eq!(re.engine(), Engine::Real, "{pat} must run on REAL, not fall back");
    }
}

#[test]
fn delegated_captures_and_names_match_regex() {
    let re = fb(r"(?P<w>\p{Word_Break=ALetter}+)\s+(\p{Word_Break=ALetter}+)");
    let std = regex::Regex::new(r"(?P<w>\p{Word_Break=ALetter}+)\s+(\p{Word_Break=ALetter}+)").unwrap();
    let text = "café oui";
    let c = re.captures(text).unwrap();
    let s = std.captures(text).unwrap();
    assert_eq!(&c["w"], &s["w"]);
    assert_eq!(c.get(2).map(|m| m.as_str()), s.get(2).map(|m| m.as_str()));
    assert_eq!(re.capture_names().collect::<Vec<_>>(), std.capture_names().collect::<Vec<_>>());
    assert_eq!(re.captures_len(), std.captures_len());
}

#[test]
fn without_fallback_flag_still_rejects() {
    // The feature is on, but a builder without fallback(true) stays strict -> Unsupported.
    let e = RegexBuilder::new(r"\p{Word_Break=ALetter}+").build().unwrap_err();
    assert!(e.is_unsupported());
    assert!(Regex::new(r"\p{Word_Break=ALetter}+").is_err()); // Regex::new is always strict
}

#[test]
fn backreference_stays_error_even_with_fallback() {
    // The regex crate has no backreferences either, so delegating one still fails — fallback is not magic.
    assert!(RegexBuilder::new(r"(\w+)\1").fallback(true).build().is_err());
}
