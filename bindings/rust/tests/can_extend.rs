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
