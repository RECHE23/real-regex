//! Differential fuzz target: real-regex vs the regex crate. Raw bytes become a (pattern, text) pair (the same
//! shape the C++ fuzzers use); coverage-guidance explores the valid-pattern space. On every pattern BOTH
//! engines accept, their results must agree — spans, captures group-by-group, a $-template replace_all, and
//! split (which stresses the wrapper's empty-match rule). A mismatch panics, and libFuzzer minimizes the
//! input to a committed reproducer.
//!
//! Filtered out (not comparable): a pattern real-regex rejects — an invalid one, or one it cannot run
//! linearly (\p{...}, a bounded lookaround real accepts but regex does not, a backreference); and any pattern
//! the regex crate rejects. Non-UTF-8 splits are skipped (the str API needs UTF-8; the bytes API is future).
#![no_main]
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    if data.is_empty() {
        return;
    }
    let body = &data[1..];
    let pattern_len = (data[0] as usize) % (body.len() + 1);
    let (pat_bytes, text_bytes) = body.split_at(pattern_len);
    let (pattern, text) = match (std::str::from_utf8(pat_bytes), std::str::from_utf8(text_bytes)) {
        (Ok(p), Ok(t)) => (p, t),
        _ => return,
    };

    let re = match real_regex::Regex::new(pattern) {
        Ok(r) => r,
        Err(_) => return, // real rejects (invalid, or unsupported: \p{}, lookaround, backref)
    };
    let std = match regex::Regex::new(pattern) {
        Ok(r) => r,
        Err(_) => return, // regex rejects
    };

    let ours: Vec<_> = re.find_iter(text).map(|m| (m.start(), m.end())).collect();
    let theirs: Vec<_> = std.find_iter(text).map(|m| (m.start(), m.end())).collect();
    assert_eq!(ours, theirs, "find_iter spans differ; pat={pattern:?} text={text:?}");

    let ours: Vec<Vec<_>> = re.captures_iter(text)
        .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect()).collect();
    let theirs: Vec<Vec<_>> = std.captures_iter(text)
        .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect()).collect();
    assert_eq!(ours, theirs, "captures differ; pat={pattern:?} text={text:?}");

    assert_eq!(re.replace_all(text, "<$0>"), std.replace_all(text, "<$0>"),
               "replace_all differs; pat={pattern:?} text={text:?}");

    let ours: Vec<_> = re.split(text).collect();
    let theirs: Vec<_> = std.split(text).collect();
    assert_eq!(ours, theirs, "split differs; pat={pattern:?} text={text:?}");
});
