// Regex::can_extend: whether the anchored match could change with more text, as the engine answers it.
use real_regex::Regex;

#[test]
fn a_token_the_next_byte_decides_is_final() {
    let re = Regex::new("[a-z]+").unwrap();
    assert!(re.can_extend("ab", 0));
    assert!(!re.can_extend("ab ", 0));
    assert!(!re.can_extend("ab ", 2)); // no match there, and no text can make one
}

#[test]
fn what_reads_the_end_waits() {
    assert!(Regex::new(r"\w+\b").unwrap().can_extend("ab", 0)); // the boundary reads the next character
    assert!(Regex::new("ab|a").unwrap().can_extend("a", 0)); // the preferred alternative is still live
    assert!(!Regex::new("a|ab").unwrap().can_extend("a", 0)); // leftmost-first: `a` wins whatever follows
    assert!(Regex::new("ab$").unwrap().can_extend("ab", 0));
    assert!(!Regex::new("ab$").unwrap().can_extend("abc", 0));
}

#[test]
fn bytes_answer_the_same() {
    let re = real_regex::bytes::Regex::new("[a-z]+").unwrap();
    assert!(re.can_extend(b"ab", 0));
    assert!(!re.can_extend(b"ab\xff", 0));
}

#[test]
fn left_context_bounds_what_a_match_reads_before_its_start() {
    // An upper bound: 0 when nothing before the start is read, else at least what the pattern reads there.
    assert_eq!(Regex::new("abc").unwrap().left_context(), 0);
    assert!(Regex::new("(?<=ab)c").unwrap().left_context() >= 2);
    assert!(Regex::new(r"\bab").unwrap().left_context() >= 1); // the boundary reads the character before
    assert_eq!(
        real_regex::bytes::Regex::new("(?<=ab)c").unwrap().left_context(),
        Regex::new("(?<=ab)c").unwrap().left_context()
    );
}
