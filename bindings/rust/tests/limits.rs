//! RegexBuilder::size_limit and dfa_size_limit hold a pattern to limits lower than the engine's, as the regex
//! crate's do: a program past the size fails with Error::CompiledTooBig (the bytes given), which no fallback
//! delegates; the DFA's memory bounds its cache, not the answer; a pattern past the engine's own limits is
//! CompiledTooBig too, with the engine's size.
use real_regex::{bytes, Error, Regex, RegexBuilder, INSTRUCTION_BYTES};

#[test]
fn size_limit_refuses_a_program_past_it() {
    let small = 100 * INSTRUCTION_BYTES;
    assert!(RegexBuilder::new(&"x".repeat(20)).size_limit(small).build().is_ok());
    assert_eq!(RegexBuilder::new(&"x".repeat(150)).size_limit(small).build().unwrap_err(), Error::CompiledTooBig(small));
    // Under one instruction, as the regex crate's size_limit(0): every pattern is too big.
    assert_eq!(RegexBuilder::new("a").size_limit(0).build().unwrap_err(), Error::CompiledTooBig(0));
    assert!(bytes::RegexBuilder::new(&"x".repeat(20)).size_limit(small).build().is_ok());
    assert_eq!(bytes::RegexBuilder::new(&"x".repeat(150)).size_limit(small).build().err(),
               Some(Error::CompiledTooBig(small)));
}

#[test]
fn the_engines_own_limits_are_compiled_too_big() {
    let err = Regex::new("a{2000}").unwrap_err();
    assert_eq!(err, Error::CompiledTooBig(262_144 * INSTRUCTION_BYTES));
    assert!(!err.is_unsupported());
    assert!(err.to_string().contains("resource limit"));
    assert!(matches!(Regex::new("(a").unwrap_err(), Error::Syntax { .. }));
}

#[test]
fn dfa_size_limit_bounds_the_cache_not_the_answer() {
    let pattern = r"\b(?:alpha|beta|gamma)\w*\d";
    let text = format!("{}gamma7", "x ".repeat(5000));
    let plain = Regex::new(pattern).unwrap();
    for limit in [1usize, 4096, 1 << 30] {
        let small = RegexBuilder::new(pattern).dfa_size_limit(limit).build().unwrap();
        assert_eq!(small.find(&text).map(|m| m.as_str()), plain.find(&text).map(|m| m.as_str()));
    }
    let bytes_small = bytes::RegexBuilder::new(pattern).dfa_size_limit(4096).build().unwrap();
    assert!(bytes_small.is_match(text.as_bytes()));
}

#[cfg(feature = "fallback")]
#[test]
fn no_fallback_runs_a_pattern_past_a_limit() {
    let err = RegexBuilder::new(&"x".repeat(150)).size_limit(100 * INSTRUCTION_BYTES).fallback(true).build().unwrap_err();
    assert_eq!(err, Error::CompiledTooBig(100 * INSTRUCTION_BYTES));
}
