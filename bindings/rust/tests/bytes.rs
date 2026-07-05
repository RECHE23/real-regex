//! Differential oracle for RD.1c: real_regex::bytes must match regex::bytes (in ASCII/byte mode) on spans,
//! captures, replace and split — including a haystack that is not valid UTF-8.
use real_regex::bytes::{NoExpand, Regex, RegexBuilder};

fn oracle(pat: &str) -> regex::bytes::Regex {
    // REAL's byte mode is ASCII for \w \d \s; match that with unicode(false).
    regex::bytes::RegexBuilder::new(pat).unicode(false).build().unwrap()
}

const CASES: &[(&str, &[u8])] = &[
    (r"[a-z]+", b"the quick brown fox"),
    (r"\d+", b"a1 bb22 ccc333"),
    (r"(\w+)@(\w+)", b"a@b and cd@ef, x@y"),
    (r"[^,]+", b"alpha,beta,,gamma"),
    (r"[a-z]+", b"\xff abc \xfe def \x00"), // not valid UTF-8
    (r"(?P<k>\w+)=(?P<v>\w+)", b"a=1 bb=22"),
];

#[test]
fn find_iter_and_captures_agree() {
    for &(pat, text) in CASES {
        let re = Regex::new(pat).unwrap();
        let std = oracle(pat);
        let ours: Vec<_> = re.find_iter(text).map(|m| (m.start(), m.end())).collect();
        let theirs: Vec<_> = std.find_iter(text).map(|m| (m.start(), m.end())).collect();
        assert_eq!(ours, theirs, "bytes find_iter {pat:?}");

        let ours: Vec<Vec<_>> = re.captures_iter(text)
            .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect()).collect();
        let theirs: Vec<Vec<_>> = std.captures_iter(text)
            .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect()).collect();
        assert_eq!(ours, theirs, "bytes captures_iter {pat:?}");

        assert_eq!(re.is_match(text), std.is_match(text), "bytes is_match {pat:?}");
    }
}

#[test]
fn replace_and_split_agree() {
    let re = Regex::new(r"(\w+)@(\w+)").unwrap();
    let std = oracle(r"(\w+)@(\w+)");
    let text = b"a@b cd@ef";
    assert_eq!(re.replace_all(text, &b"$2.$1"[..]), std.replace_all(text, &b"$2.$1"[..]), "bytes replace_all");
    assert_eq!(re.replace_all(text, NoExpand(b"#")), std.replace_all(text, regex::bytes::NoExpand(b"#")), "bytes NoExpand");

    let sr = Regex::new(r",").unwrap();
    let ss = oracle(r",");
    let t = b"a,b,,c,";
    assert_eq!(sr.split(t).collect::<Vec<_>>(), ss.split(t).collect::<Vec<_>>(), "bytes split");
}

#[test]
fn named_lookup_and_builder_agree() {
    let re = Regex::new(r"(?P<k>\w+)=(?P<v>\w+)").unwrap();
    let c = re.captures(b"key=val").unwrap();
    assert_eq!(&c["k"], b"key");
    assert_eq!(&c["v"], b"val");
    assert_eq!(re.capture_names().collect::<Vec<_>>(), vec![None, Some("k"), Some("v")]);

    let re = RegexBuilder::new(r"abc").case_insensitive(true).build().unwrap();
    let std = regex::bytes::RegexBuilder::new(r"abc").unicode(false).case_insensitive(true).build().unwrap();
    assert_eq!(re.is_match(b"ABC"), std.is_match(b"ABC"));
}
