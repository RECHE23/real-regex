//! Every short pattern over the characters the two grammars treat specially, against the `regex` crate.
//!
//! A pattern both accept must mean the same to both; where the grammars part ways the crate declines it
//! (`Error::Unsupported`) rather than matching something else. A range from a class's leading `]` was such
//! a place and went unseen for months by the fuzzer, which rarely draws one: here every string of up to
//! three characters is drawn, bare and inside `[…]` and `[^…]`, about 46 000 patterns.
//!
//! The possessive quantifiers (`x?+`, `x*+`, `x++`) are routed around: both compile them and read them
//! differently on purpose (`tests/possessive.rs` pins both readings).

use real_regex::Regex;

const ALPHABET: &[char] = &['[', ']', '^', '-', '\\', '&', '~', '.', '|', '(', ')', '*', '+', '?', '{', '}', ',', ':',
                            'a', 'b', '1', 'd', 'w', 'x'];

const SUBJECTS: &[&str] = &["ab-]^[\\&~.|()*+?{},:1 dwx\nA_ é", "", "]]--^^", "a{1,2}b", "x:dw"];

// A quantifier right after another quantifier, outside a class: the possessive form.
fn is_possessive(pattern: &str) -> bool {
    let chars: Vec<char> = pattern.chars().collect();
    let mut in_class = false;
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c == '\\' {
            i += 2;
            continue;
        }
        if in_class {
            in_class = c != ']';
        }
        else if c == '[' {
            in_class = true;
        }
        else if matches!(c, '?' | '*' | '+' | '}') && chars.get(i + 1) == Some(&'+') {
            return true;
        }
        i += 1;
    }
    false
}

fn spans(re: &Regex, text: &str) -> Vec<(usize, usize)> {
    re.find_iter(text).map(|m| (m.start(), m.end())).collect()
}

fn oracle_spans(re: &regex::Regex, text: &str) -> Vec<(usize, usize)> {
    re.find_iter(text).map(|m| (m.start(), m.end())).collect()
}

#[test]
fn every_short_pattern_both_accept_means_the_same() {
    let mut bodies = vec![String::new()];
    let mut patterns = Vec::new();
    for _ in 0..3 {
        bodies = bodies.iter().flat_map(|body| ALPHABET.iter().map(move |&c| format!("{body}{c}"))).collect();
        patterns.extend(bodies.iter().cloned());
    }
    let bare = patterns.len();
    for i in 0..bare {
        let body = patterns[i].clone();
        patterns.push(format!("[{body}]"));
        patterns.push(format!("[^{body}]"));
    }
    let mut compared = 0;
    let mut divergent = Vec::new();
    for pattern in patterns.iter().filter(|p| !is_possessive(p)) {
        let (Ok(ours), Ok(theirs)) = (Regex::new(pattern), regex::Regex::new(pattern)) else { continue };
        compared += 1;
        if let Some(text) = SUBJECTS.iter().find(|text| spans(&ours, text) != oracle_spans(&theirs, text)) {
            divergent.push(format!("{pattern:?} on {text:?}: ours {:?}, regex {:?}", spans(&ours, text),
                                   oracle_spans(&theirs, text)));
        }
    }
    assert!(compared > 25_000, "only {compared} patterns compared: the sweep lost its reach");
    assert!(divergent.is_empty(), "{} divergent pattern(s), the first: {:#?}", divergent.len(),
            &divergent[..divergent.len().min(10)]);
}
