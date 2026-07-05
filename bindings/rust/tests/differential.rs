//! Differential oracle: on patterns both engines support, real-regex must agree with the `regex` crate on
//! every match span, every capture group, group names, and the query methods. Each mirror method is checked
//! here — a faulty mirror makes the oracle bite.
use real_regex::Regex;

const CASES: &[(&str, &str)] = &[
    (r"[a-z]+", "the quick brown fox"),
    (r"\d+", "a1 bb22 ccc333"),
    (r"(\w+)@(\w+)", "a@b and cd@ef, x@y"),
    (r"(\d{4})-(\d{2})-(\d{2})", "2026-07-04 and 1999-12-31"),
    (r"[^,]+", "alpha,beta,,gamma"),
    (r"(a|bb|ccc)+", "abbcccabb"),
    (r"\bword\b", "a word in words and word."),
    (r"(?P<user>\w+)@(?P<host>\w+)", "root@localhost, guest@remote"),
    (r"(a)(b)?(c)", "ac abc ac"), // an optional group that sometimes does not participate
];

fn spans_of(m: Option<real_regex::Match>) -> Option<(usize, usize)> {
    m.map(|m| (m.start(), m.end()))
}

#[test]
fn find_iter_spans_agree() {
    for &(pat, text) in CASES {
        let re = Regex::new(pat).unwrap_or_else(|e| panic!("real-regex rejected {pat:?}: {e}"));
        let std = regex::Regex::new(pat).unwrap();
        let ours: Vec<_> = re.find_iter(text).map(|m| (m.start(), m.end())).collect();
        let theirs: Vec<_> = std.find_iter(text).map(|m| (m.start(), m.end())).collect();
        assert_eq!(ours, theirs, "find_iter spans differ for {pat:?}");
    }
}

#[test]
fn captures_iter_groups_agree() {
    for &(pat, text) in CASES {
        let re = Regex::new(pat).unwrap();
        let std = regex::Regex::new(pat).unwrap();
        let ours: Vec<Vec<_>> = re
            .captures_iter(text)
            .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect())
            .collect();
        let theirs: Vec<Vec<_>> = std
            .captures_iter(text)
            .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect())
            .collect();
        assert_eq!(ours, theirs, "captures_iter groups differ for {pat:?}");
    }
}

#[test]
fn find_and_captures_first_agree() {
    for &(pat, text) in CASES {
        let re = Regex::new(pat).unwrap();
        let std = regex::Regex::new(pat).unwrap();
        assert_eq!(spans_of(re.find(text)), std.find(text).map(|m| (m.start(), m.end())), "find {pat:?}");
        assert_eq!(re.is_match(text), std.is_match(text), "is_match {pat:?}");
        let ours = re.captures(text).map(|c| c.get(0).map(|m| (m.start(), m.end())));
        let theirs = std.captures(text).map(|c| c.get(0).map(|m| (m.start(), m.end())));
        assert_eq!(ours, theirs, "captures(0) {pat:?}");
    }
}

#[test]
fn find_at_agrees() {
    for &(pat, text) in CASES {
        let re = Regex::new(pat).unwrap();
        let std = regex::Regex::new(pat).unwrap();
        for start in [0usize, 1, 3, text.len() / 2] {
            if !text.is_char_boundary(start) {
                continue;
            }
            assert_eq!(
                spans_of(re.find_at(text, start)),
                std.find_at(text, start).map(|m| (m.start(), m.end())),
                "find_at({start}) {pat:?}"
            );
        }
    }
}

#[test]
fn capture_names_and_named_lookup_agree() {
    let pat = r"(?P<user>\w+)@(?P<host>\w+)";
    let text = "root@localhost";
    let re = Regex::new(pat).unwrap();
    let std = regex::Regex::new(pat).unwrap();

    let ours: Vec<_> = re.capture_names().collect();
    let theirs: Vec<_> = std.capture_names().collect();
    assert_eq!(ours, theirs, "capture_names differ");
    assert_eq!(re.captures_len(), std.captures_len());

    let c = re.captures(text).unwrap();
    let s = std.captures(text).unwrap();
    assert_eq!(&c["user"], &s["user"]);
    assert_eq!(&c["host"], &s["host"]);
    assert_eq!(c.name("user").map(|m| m.as_str()), s.name("user").map(|m| m.as_str()));
    assert_eq!(&c[0], &s[0]);
    assert_eq!(&c[1], &s[1]);
}

#[test]
fn strict_rejects_a_backreference() {
    // A backreference cannot run linearly: the strict engine rejects it (regex rejects it too, for lack of
    // support) — either way, real-regex never silently backtracks.
    assert!(Regex::new(r"(\w+)\1").is_err());
}

#[test]
fn optional_group_reports_none_like_regex() {
    // (a)(b)?(c) on "ac": group 2 does not participate -> None, exactly as regex reports it.
    let re = Regex::new(r"(a)(b)?(c)").unwrap();
    let c = re.captures("ac").unwrap();
    assert_eq!(c.get(2), None);
    assert_eq!(c.get(1).unwrap().as_str(), "a");
    assert_eq!(c.get(3).unwrap().as_str(), "c");
}
