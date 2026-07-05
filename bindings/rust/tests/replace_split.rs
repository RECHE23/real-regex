//! Differential oracle for RD.1b: replace / split / RegexBuilder must match the regex crate's results.
use real_regex::{NoExpand, Regex, RegexBuilder};

#[test]
fn replace_all_templates_agree() {
    let cases: &[(&str, &str, &str)] = &[
        (r"(\w+)@(\w+)", "$2.$1", "a@b and cd@ef"),
        (r"(?P<y>\d{4})-(?P<m>\d{2})", "${m}/${y}", "2026-07 x 1999-12"),
        (r"\s+", "_", "a  b   c"),
        (r"(a)(b)?(c)", "[$1$2$3]", "ac abc"),          // $2 absent -> empty
        (r"x", "$$", "xx"),                              // $$ -> literal $
    ];
    for &(pat, rep, text) in cases {
        let re = Regex::new(pat).unwrap();
        let std = regex::Regex::new(pat).unwrap();
        assert_eq!(re.replace_all(text, rep), std.replace_all(text, rep), "replace_all {pat:?} {rep:?}");
        assert_eq!(re.replace(text, rep), std.replace(text, rep), "replace {pat:?} {rep:?}");
        assert_eq!(re.replacen(text, 2, rep), std.replacen(text, 2, rep), "replacen {pat:?} {rep:?}");
    }
}

#[test]
fn replace_closure_and_noexpand_agree() {
    let re = Regex::new(r"\d+").unwrap();
    let std = regex::Regex::new(r"\d+").unwrap();
    let text = "a1 b22 c333";
    let ours = re.replace_all(text, |c: &real_regex::Captures| format!("<{}>", c[0].len()));
    let theirs = std.replace_all(text, |c: &regex::Captures| format!("<{}>", c[0].len()));
    assert_eq!(ours, theirs, "closure replace");
    assert_eq!(re.replace_all(text, NoExpand("#")), std.replace_all(text, regex::NoExpand("#")), "NoExpand");
}

#[test]
fn split_and_splitn_agree() {
    let cases: &[(&str, &str)] = &[
        (r",", "a,b,,c,"),
        (r"\s+", "  the quick  brown "),
        (r"\d+", "abc123def456ghi"),
        (r"x", "no delimiter here"),
    ];
    for &(pat, text) in cases {
        let re = Regex::new(pat).unwrap();
        let std = regex::Regex::new(pat).unwrap();
        let ours: Vec<_> = re.split(text).collect();
        let theirs: Vec<_> = std.split(text).collect();
        assert_eq!(ours, theirs, "split {pat:?}");
        for n in [0usize, 1, 2, 3] {
            let ours: Vec<_> = re.splitn(text, n).collect();
            let theirs: Vec<_> = std.splitn(text, n).collect();
            assert_eq!(ours, theirs, "splitn({n}) {pat:?}");
        }
    }
}

#[test]
fn regex_builder_flags_agree() {
    // case_insensitive
    let re = RegexBuilder::new(r"abc").case_insensitive(true).build().unwrap();
    let std = regex::RegexBuilder::new(r"abc").case_insensitive(true).build().unwrap();
    assert_eq!(re.is_match("ABC"), std.is_match("ABC"));
    // multi_line
    let re = RegexBuilder::new(r"^b").multi_line(true).build().unwrap();
    let std = regex::RegexBuilder::new(r"^b").multi_line(true).build().unwrap();
    let text = "a\nb\nc";
    assert_eq!(re.find_iter(text).map(|m| m.start()).collect::<Vec<_>>(),
               std.find_iter(text).map(|m| m.start()).collect::<Vec<_>>());
    // dot_matches_new_line
    let re = RegexBuilder::new(r"a.b").dot_matches_new_line(true).build().unwrap();
    let std = regex::RegexBuilder::new(r"a.b").dot_matches_new_line(true).build().unwrap();
    assert_eq!(re.is_match("a\nb"), std.is_match("a\nb"));
    // unicode(false): \w restricted to ASCII
    let re = RegexBuilder::new(r"\w+").unicode(false).build().unwrap();
    let std = regex::RegexBuilder::new(r"\w+").unicode(false).build().unwrap();
    let text = "café";
    assert_eq!(re.find(text).map(|m| m.as_str()), std.find(text).map(|m| m.as_str()), "unicode(false) \\w");
}
