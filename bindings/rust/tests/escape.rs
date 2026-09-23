//! `escape` is the regex crate's: the same output, and a pattern that matches its input literally.
use real_regex::{bytes, escape, Regex};

#[test]
fn escape_is_the_crates_and_matches_literally() {
    let mut subjects: Vec<String> = (1u8..128).map(|b| (b as char).to_string()).collect();
    subjects.extend(["a.b*c", "(x|y)[z]{1,2}", "^$", "#&-~", "\\d", "café ☃ 😀", "a b\tc\n", ""].map(String::from));
    let all_ascii: String = (1u8..128).map(|b| b as char).collect();
    subjects.push(all_ascii);
    for s in &subjects {
        let quoted = escape(s);
        assert_eq!(quoted, regex::escape(s), "{s:?}: differs from regex::escape");
        let whole = Regex::new(&format!("^(?:{quoted})$")).unwrap_or_else(|e| panic!("{s:?} -> {quoted:?}: {e}"));
        assert!(whole.is_match(s), "{s:?}: the escaped pattern {quoted:?} does not match its own input");
        let found = Regex::new(&quoted).unwrap().find(s).map(|m| (m.start(), m.end()));
        assert_eq!(found, Some((0, s.len())), "{s:?}: the escaped pattern does not span the input");
        let raw = bytes::Regex::new(&quoted).unwrap();
        assert_eq!(raw.find(s.as_bytes()).map(|m| (m.start(), m.end())), Some((0, s.len())), "{s:?}: bytes mode");
    }
}
