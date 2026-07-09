//! count_matches parity with find_iter (and trailing-LA surface).
use real_regex::Regex;

#[test]
fn count_matches_equals_find_iter_count() {
    let re = Regex::new(r"\d+").unwrap();
    let text = "a1 b22 c333 d4";
    assert_eq!(re.count_matches(text), re.find_iter(text).count());
    assert_eq!(re.count_matches(text), 4);
}

#[test]
fn count_matches_trailing_la() {
    // Trailing-LA class+: same count as find_iter (fast path on count_matches only).
    let re = Regex::new(r"[a-z]+(?=[a-z])").unwrap();
    let text = "abc def ghi";
    assert_eq!(re.count_matches(text), re.find_iter(text).count());
    assert_eq!(re.count_matches("abc def"), 2);
}

#[test]
fn count_matches_empty() {
    let re = Regex::new(r"zzz").unwrap();
    assert_eq!(re.count_matches("nothing here"), 0);
}
