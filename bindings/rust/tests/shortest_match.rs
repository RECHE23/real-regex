//! shortest_match is a documented divergence: REAL is leftmost-first and returns the greedy end, where the
//! regex crate returns the earliest completion.
#[test]
fn shortest_match_returns_greedy_end() {
    let re = real_regex::Regex::new(r"a+").unwrap();
    let std = regex::Regex::new(r"a+").unwrap();
    assert_eq!(re.shortest_match("aaa"), Some(3));   // REAL: greedy end
    assert_eq!(std.shortest_match("aaa"), Some(1));  // regex: earliest completion
    assert_ne!(re.shortest_match("aaa"), std.shortest_match("aaa"));
}
