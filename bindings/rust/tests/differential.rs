//! Differential oracle: on patterns both engines support, real-regex must agree with the `regex` crate on
//! every match span and every capture group.
use real_regex::Regex;

const CASES: &[(&str, &str)] = &[
    (r"[a-z]+", "the quick brown fox"),
    (r"\d+", "a1 bb22 ccc333"),
    (r"(\w+)@(\w+)", "a@b and cd@ef, x@y"),
    (r"(\d{4})-(\d{2})-(\d{2})", "2026-07-04 and 1999-12-31"),
    (r"[^,]+", "alpha,beta,,gamma"),
    (r"(a|bb|ccc)+", "abbcccabb"),
    (r"\bword\b", "a word in words and word."),
];

#[test]
fn agrees_with_regex_crate_on_spans_and_captures() {
    for &(pat, text) in CASES {
        let re = Regex::new(pat).unwrap_or_else(|e| panic!("real-regex rejected {pat:?}: {e}"));
        let std = regex::Regex::new(pat).unwrap();

        let ours: Vec<_> = re.find_iter(text).map(|m| m.span(0)).collect();
        let theirs: Vec<_> = std.find_iter(text).map(|m| Some((m.start(), m.end()))).collect();
        assert_eq!(ours, theirs, "match spans differ for {pat:?}");

        // captures, group by group
        let our_caps: Vec<Vec<_>> = re
            .find_iter(text)
            .map(|m| (0..m.len()).map(|g| m.span(g)).collect())
            .collect();
        let their_caps: Vec<Vec<_>> = std
            .captures_iter(text)
            .map(|c| {
                (0..c.len())
                    .map(|g| c.get(g).map(|s| (s.start(), s.end())))
                    .collect()
            })
            .collect();
        assert_eq!(our_caps, their_caps, "captures differ for {pat:?}");
    }
}

#[test]
fn strict_rejects_a_backreference() {
    // A backreference cannot run linearly: the strict engine rejects it (the regex crate rejects it too,
    // for lack of support) — either way, real-regex never silently backtracks.
    assert!(Regex::new(r"(\w+)\1").is_err());
}

#[test]
fn is_match_and_captures_len() {
    let re = Regex::new(r"(\d+)-(\d+)").unwrap();
    assert!(re.is_match("12-34"));
    assert!(!re.is_match("no digits here"));
    assert_eq!(re.captures_len(), 2);
}
