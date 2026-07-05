//! RD.1-fix: the rust empty-match iteration rule (an empty match adjacent to the previous match end is
//! skipped). REAL's engine follows re's rule (keeps it); the wrapper adapts it to rust's contract. This is
//! the empty-capable coverage the first differentials lacked, plus the three vicious cases that found it.
use real_regex::Regex;

// Empty-capable patterns across texts with and without borders / interior empties.
const EMPTY_CAPABLE: &[(&str, &str)] = &[
    (r"x*", "axbxc"),
    (r"x*", "xx"),
    (r"a*", ""),
    (r"a*", "baaab"),
    (r"(x?)", "axbx"),
    (r"\b", "one two"),
    (r"$", "a\nb"),           // end anchors
    // Empty-PRIORITY alternations: a leftmost empty branch matches empty at p, and re then re-tries a
    // non-empty at p that rust's finder never returns (it advances past the empty). The RD.3 fuzzer's class.
    (r"|x", "x"),
    (r"x|", "xyx"),
    (r"a||b", "ab"),
    (r"(?:|x)", "xx"),
    ("|\u{1}", "\u{1}"),      // the minimized 4-byte fuzzer artifact: |\x01 on \x01
    // Empty-STAR: a nullable loop over an empty-first branch. rust visits positions the re-stream never does
    // (`(?:|ab)*` on "abab": rust yields empties at 1 and 3; re advances by its own wider (0,2)/(2,4) matches
    // and skips them). No filter over re's stream can ADD those — RD.3-fix-v2 drives the search by position.
    (r"(?:|ab)*", "abab"),
    (r"(a?)*", "aa"),
    (r"(|a)+", "aa"),
    (r"(?:|ab)*", "abcabc"), // an interior non-match between the ab runs
    ("(?:|\u{e9})*", "\u{e9}\u{e9}"), // non-ASCII: empty steps must advance a full codepoint (\u00e9 = 2 bytes)
];

#[test]
fn empty_capable_find_iter_matches_rust() {
    for &(pat, text) in EMPTY_CAPABLE {
        let re = Regex::new(pat).unwrap();
        let std = regex::Regex::new(pat).unwrap();
        let ours: Vec<_> = re.find_iter(text).map(|m| (m.start(), m.end())).collect();
        let theirs: Vec<_> = std.find_iter(text).map(|m| (m.start(), m.end())).collect();
        assert_eq!(ours, theirs, "empty-capable find_iter {pat:?} on {text:?}");
    }
}

#[test]
fn empty_capable_split_and_replace_match_rust() {
    for &(pat, text) in EMPTY_CAPABLE {
        let re = Regex::new(pat).unwrap();
        let std = regex::Regex::new(pat).unwrap();
        assert_eq!(re.split(text).collect::<Vec<_>>(), std.split(text).collect::<Vec<_>>(),
                   "split {pat:?} on {text:?}");
        assert_eq!(re.replace_all(text, "#"), std.replace_all(text, "#"),
                   "replace_all {pat:?} on {text:?}");
    }
}

// The three vicious cases (they entered the suite because they found the divergence).
#[test]
fn vicious_split_empty() {
    // rust's rule: x* on "axbxc" -> the empties adjacent to the x-matches are skipped.
    let re = Regex::new(r"x*").unwrap();
    let std = regex::Regex::new(r"x*").unwrap();
    assert_eq!(re.split("axbxc").collect::<Vec<_>>(), std.split("axbxc").collect::<Vec<_>>());
}

#[test]
fn vicious_optional_named_group() {
    // A named group that sometimes does not participate, over an iterator.
    let re = Regex::new(r"(?P<pre>a)?b").unwrap();
    let std = regex::Regex::new(r"(?P<pre>a)?b").unwrap();
    let text = "ab b ab";
    let ours: Vec<_> = re.captures_iter(text).map(|c| c.name("pre").map(|m| (m.start(), m.end()))).collect();
    let theirs: Vec<_> = std.captures_iter(text).map(|c| c.name("pre").map(|m| (m.start(), m.end()))).collect();
    assert_eq!(ours, theirs);
}

#[test]
fn vicious_adjacent_captures() {
    // Adjacent empty-capable captures interleaved with real ones.
    let re = Regex::new(r"(a*)(b*)").unwrap();
    let std = regex::Regex::new(r"(a*)(b*)").unwrap();
    let text = "aabbab";
    let ours: Vec<Vec<_>> = re.captures_iter(text)
        .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect()).collect();
    let theirs: Vec<Vec<_>> = std.captures_iter(text)
        .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect()).collect();
    assert_eq!(ours, theirs);
}
