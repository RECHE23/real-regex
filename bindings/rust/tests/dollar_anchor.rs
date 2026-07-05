//! RD.3-fix-v3: `$` semantics. Python `re`'s `$` matches at the end OR just before a final `\n`; rust's `$`
//! is end-only (`\z`). The crate compiles every pattern with real::flags::dollar_endonly, so `$` carries
//! rust's semantics — otherwise `a$` on "a\n" would match (re) where rust finds nothing. Engine default is
//! untouched (the flag is additive); only the crate opts in.
use real_regex::Regex;

const DOLLAR: &[(&str, &str)] = &[
    (r"a$", "a\n"),   // the fuzzer's case: rust yields nothing, not [(0,1)]
    (r"a$", "a"),
    (r"a$", "a\nb"),
    (r"a$", "a\n\n"),
    (r"$", "x\n"),
    (r"$", "x"),
    (r"$$", "x\n"),
    (r"(?m)a$", "a\nba\n"), // multiline: before each \n AND end — both engines aligned
    (r"^a$", "a\n"),
];

#[test]
fn dollar_matches_rust() {
    for &(pat, text) in DOLLAR {
        let re = Regex::new(pat).unwrap();
        let std = regex::Regex::new(pat).unwrap();
        let ours: Vec<_> = re.find_iter(text).map(|m| (m.start(), m.end())).collect();
        let theirs: Vec<_> = std.find_iter(text).map(|m| (m.start(), m.end())).collect();
        assert_eq!(ours, theirs, "$ {pat:?} on {text:?}");
    }
}

#[test]
fn dollar_bytes_matches_rust() {
    let cases: &[(&str, &[u8])] = &[("a$", b"a\n"), ("a$", b"a"), ("(?m)a$", b"a\nb")];
    for &(pat, text) in cases {
        let re = real_regex::bytes::Regex::new(pat).unwrap();
        let std = regex::bytes::Regex::new(pat).unwrap();
        let ours: Vec<_> = re.find_iter(text).map(|m| (m.start(), m.end())).collect();
        let theirs: Vec<_> = std.find_iter(text).map(|m| (m.start(), m.end())).collect();
        assert_eq!(ours, theirs, "$ bytes {pat:?} on {text:?}");
    }
}
