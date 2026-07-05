//! RD.2b: the fallback feature. A pattern REAL cannot run linearly is delegated to the regex crate (opt-in,
//! per pattern), through our own types; the results must equal bare regex. Runs only with --features fallback.
#![cfg(feature = "fallback")]
use real_regex::{Engine, Regex, RegexBuilder};

fn fb(pat: &str) -> Regex {
    RegexBuilder::new(pat).fallback(true).build().unwrap()
}

#[test]
fn unicode_property_delegates_and_is_observable() {
    let re = fb(r"\p{L}+");
    assert_eq!(re.engine(), Engine::Fallback, "\\p{{L}} must be delegated");
    let std = regex::Regex::new(r"\p{L}+").unwrap();
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
fn delegated_captures_and_names_match_regex() {
    let re = fb(r"(?P<w>\p{L}+)\s+(\p{N}+)");
    let std = regex::Regex::new(r"(?P<w>\p{L}+)\s+(\p{N}+)").unwrap();
    let text = "café 42";
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
    let e = RegexBuilder::new(r"\p{L}+").build().unwrap_err();
    assert!(e.is_unsupported());
    assert!(Regex::new(r"\p{L}+").is_err()); // Regex::new is always strict
}
