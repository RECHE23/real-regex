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
use std::collections::HashSet;
use std::sync::LazyLock;

/// Code points where REAL's `\w`/`\s` (Python `re` semantics — its contract) and the `regex` crate's (UTS#18)
/// disagree: marks `\p{M}`, `\p{No}`, `U+001C`-`U+001F`, Join_Control, and more (see the README divergence
/// table). Computed by asking BOTH engines, so it needs no hardcoded category table and self-updates with the
/// Unicode version. Built once; `fuzz/unicode_probe/` is the committed, category-annotated audit of the same set.
static WORD_SPACE_DELTAS: LazyLock<HashSet<char>> = LazyLock::new(|| {
    let rw = regex::Regex::new(r"^\w$").unwrap();
    let ow = real_regex::Regex::new(r"^\w$").unwrap();
    let rs = regex::Regex::new(r"^\s$").unwrap();
    let os = real_regex::Regex::new(r"^\s$").unwrap();
    let mut set = HashSet::new();
    for cp in 0u32..=0x0010_FFFF {
        if let Some(c) = char::from_u32(cp) {
            let s = c.to_string();
            if rw.is_match(&s) != ow.is_match(&s) || rs.is_match(&s) != os.is_match(&s) {
                set.insert(c);
            }
        }
    }
    set
});

/// Whether `pattern` uses a `\w`/`\s`-family shorthand (`\w \W \s \S \b \B`) — `\d`/`\D` are identical in both
/// engines, so they do not need the delta skip. A backslash-escape scan; a `\b` inside a class (backspace) is a
/// harmless over-approximation.
fn uses_word_or_space_class(pattern: &str) -> bool {
    let mut escaped = false;
    for c in pattern.chars() {
        if escaped {
            if matches!(c, 'w' | 'W' | 's' | 'S' | 'b' | 'B') {
                return true;
            }
            escaped = false;
        } else if c == '\\' {
            escaped = true;
        }
    }
    false
}

/// Whether `pattern` has an alternation `|` at the top level — outside every group and character class. Used to
/// skip the regex-crate leftmost-first bug class (see below); a `|` inside `(...)` or `[...]` does not trigger
/// it, so depth and class state are tracked rather than a plain substring search.
fn has_top_level_alternation(pattern: &str) -> bool {
    let mut depth = 0i32;
    let mut in_class = false;
    let mut escaped = false;
    for c in pattern.chars() {
        if escaped {
            escaped = false;
        } else if c == '\\' {
            escaped = true;
        } else if in_class {
            if c == ']' {
                in_class = false;
            }
        } else {
            match c {
                '[' => in_class = true,
                '(' => depth += 1,
                ')' => depth = (depth - 1).max(0),
                '|' if depth == 0 => return true,
                _ => {}
            }
        }
    }
    false
}

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

    // Skip a known UPSTREAM regex-crate leftmost-first bug class (REAL agrees with Python re; the regex crate
    // does not): with a top-level alternation, its literal prefilter — built from one branch's leading byte —
    // can resume PAST a leftmost match that begins in another branch. Two reproducers so far: `A|.AA` on
    // "\n#AA" (the `.` branch, [1,4)); and `\0*\0|\u{8}\u{c}\0\0` on "\0\0\0|~\u{8}\u{c}\0\0\0", where the `\u{8}`
    // branch matches [5,9) but the prefilter skips to the `\0` branch at [7,10). The trigger is the top-level
    // `|`, not the branch's first token, so skip any pattern with one (the earlier `|.`/`|[` check was an
    // under-approximation that let the literal-branch case through). Alternation stays fully covered by the
    // std and re differentials and the 3.2M-case exhaustive; this only removes the rust-only bug class. See
    // fuzz/known_rust_bugs/.
    if has_top_level_alternation(pattern) {
        return;
    }

    // Skip the documented CPython-vs-UTS#18 word/space divergence (README "Divergences"): REAL's \w/\s follow
    // Python re, the regex crate follows UTS#18, and they disagree on a fixed set of code points. When a
    // \w/\W/\s/\S/\b/\B pattern meets a text carrying one, the two legitimately differ — not a REAL bug (the
    // REAL/re differential and the 3.2M-case exhaustive assert REAL's side).
    if uses_word_or_space_class(pattern) && text.chars().any(|c| WORD_SPACE_DELTAS.contains(&c)) {
        return;
    }

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
