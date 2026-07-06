//! Differential fuzz target: real-regex vs the regex crate. Raw bytes become a (pattern, text) pair (the same
//! shape the C++ fuzzers use); coverage-guidance explores the valid-pattern space. On every pattern BOTH
//! engines accept, their results must agree — spans, captures group-by-group, a $-template replace_all, and
//! split (which stresses the wrapper's empty-match rule). A mismatch panics, and libFuzzer minimizes the
//! input to a committed reproducer.
//!
//! Filtered out (not comparable): a pattern real-regex rejects — an invalid one, or one it cannot run
//! linearly (a bounded lookaround real accepts but regex does not, a backreference); and any pattern the regex
//! crate rejects. `\p{Gc}`/`\p{sc}` ARE compared now (both Unicode 16.0.0); two documented divergence classes
//! are masked by name — the \w/\s UTS#18 set and the icase Turkish-I fold set. Non-UTF-8 splits are skipped (the
//! str API needs UTF-8; the bytes API is future).
#![no_main]
use libfuzzer_sys::fuzz_target;
use std::collections::HashSet;
use std::sync::LazyLock;

/// Code points where REAL's `\w`/`\s` (Python `re` semantics — its contract) and the `regex` crate's (UTS#18)
/// disagree: marks `\p{M}`, `\p{No}`, `U+001C`-`U+001F`, Join_Control, and more (see the README divergence
/// table). Computed by asking BOTH engines, so it needs no hardcoded category table and self-updates with the
/// Unicode version. Built once; `fuzz/unicode_probe/` is the committed, category-annotated audit of the same set.
static WORD_SPACE_DELTAS: LazyLock<HashSet<char>> = LazyLock::new(|| {
    // Built once, by SCAN not per scalar: one string of every code point, one find_iter per engine per class.
    // (The per-scalar `is_match` form did 1.1M x 2 FFI round-trips and timed the fuzzer out on its first \s
    // input; each `\w`/`\s` is a one-char match, so find_iter over the whole string yields the matched set.)
    let all: String = (0u32..=0x0010_FFFF).filter_map(char::from_u32).collect();
    let rust_set = |pat: &str| -> HashSet<char> {
        regex::Regex::new(pat).unwrap().find_iter(&all).map(|m| m.as_str().chars().next().unwrap()).collect()
    };
    let real_set = |pat: &str| -> HashSet<char> {
        real_regex::Regex::new(pat).unwrap().find_iter(&all).map(|m| m.as_str().chars().next().unwrap()).collect()
    };
    let mut set = HashSet::new();
    for pat in [r"\w", r"\s"] {
        set.extend(rust_set(pat).symmetric_difference(&real_set(pat)));
    }
    set
});

/// Code points where REAL's icase folding (Python `re`'s equivalences, via upper/lower — its contract) and the
/// `regex` crate's (Unicode simple CaseFolding) disagree: the Turkish dotless/dotted I (U+0131 ı, U+0130 İ) fold
/// with I/i in `re` but stand apart in CaseFolding, and any similar case entry. Both engines are correct for
/// their contract (verified: stdlib `re.fullmatch("(?i)I","ı")` is True; see the README divergence note). When an
/// icase pattern meets one of these, the two legitimately differ — not a REAL bug. Computed by asking BOTH
/// engines over the cased letters, so it self-updates with the Unicode version and needs no hardcoded list.
static ICASE_FOLD_DELTAS: LazyLock<HashSet<char>> = LazyLock::new(|| {
    let all: String = (0u32..=0x0010_FFFF).filter_map(char::from_u32).collect();
    let rust_set = |pat: &str| -> HashSet<char> {
        regex::Regex::new(pat).unwrap().find_iter(&all).map(|m| m.as_str().chars().next().unwrap()).collect()
    };
    let real_set = |pat: &str| -> HashSet<char> {
        real_regex::Regex::new(pat).unwrap().find_iter(&all).map(|m| m.as_str().chars().next().unwrap()).collect()
    };
    let mut set = HashSet::new();
    // the cased classes under icase: a fold that differs shows up as a class-membership difference here
    for pat in [r"(?i)\p{Lu}", r"(?i)\p{Ll}", r"(?i)\p{Lt}"] {
        set.extend(rust_set(pat).symmetric_difference(&real_set(pat)));
    }
    set
});

/// Whether `pattern` turns icase on — an inline `(?i` / `(?mi` / `(?im:` flag group whose `i` is not cancelled by
/// a `-`. Used to gate the ICASE_FOLD_DELTAS skip (the fold divergence only bites under icase). An
/// over-approximation is harmless (it only skips more).
fn is_icase(pattern: &str) -> bool {
    let b = pattern.as_bytes();
    let mut i = 0;
    while i + 1 < b.len() {
        if b[i] == b'(' && b[i + 1] == b'?' {
            let mut j = i + 2;
            let mut negate = false;
            while j < b.len() && matches!(b[j], b'i' | b'm' | b's' | b'x' | b'a' | b'u' | b'L' | b'-') {
                match b[j] {
                    b'-' => negate = true,
                    b'i' if !negate => return true,
                    _ => {}
                }
                j += 1;
            }
        }
        i += 1;
    }
    false
}

/// Whether `pattern` uses a word/space-dependent shorthand (`\w \W \s \S \b \B \< \>` — all
/// derive from the same word/space sets, so any of them diverges on a delta code point) — `\d`/`\D` are identical in both
/// engines, so they do not need the delta skip. A backslash-escape scan; a `\b` inside a class (backspace) is a
/// harmless over-approximation.
fn uses_word_or_space_class(pattern: &str) -> bool {
    let mut escaped = false;
    for c in pattern.chars() {
        if escaped {
            if matches!(c, 'w' | 'W' | 's' | 'S' | 'b' | 'B' | '<' | '>') {
                return true;
            }
            escaped = false;
        } else if c == '\\' {
            escaped = true;
        }
    }
    false
}

/// The upstream regex-crate leftmost-first bug class (rust-lang/regex #1345 / #1373): its meta-engine's
/// reverse-suffix / prefilter search can resume PAST a leftmost match that REAL (and Python `re`) find. Detected
/// SEMANTICALLY, not by pattern shape (the form predicates were a losing whack-a-mole): given a find_iter
/// divergence, take the first span REAL reported that the crate's list does NOT contain; if the crate ITSELF
/// confirms a match of exactly that span when ANCHORED at its start (`^(?:pat)` on the tail), the crate's
/// unanchored search dropped a match it agrees exists — its bug. Cannot swallow a real REAL bug: a REAL span the
/// anchored crate does not confirm returns false (still panics), as does a match REAL missed. Runs only on a
/// mismatch, so it costs nothing in the normal (agreeing) case, and it self-verifies against every past and
/// future manifestation of the class.
fn crate_dropped_a_leftmost_it_confirms(pattern: &str, text: &str,
                                        ours: &[(usize, usize)], theirs: &[(usize, usize)]) -> bool {
    let &(rs, re_end) = match ours.iter().find(|s| !theirs.contains(s)) {
        Some(s) => s,
        None => return false, // no REAL span the crate lacks -> not this class
    };
    let tail = match text.get(rs..) {
        Some(t) => t,
        None => return false, // rs is not a char boundary (shouldn't happen for a real match) -> do not skip
    };
    let anchored = match regex::Regex::new(&format!("^(?:{pattern})")) {
        Ok(r) => r,
        Err(_) => return false,
    };
    anchored.find(tail).map_or(false, |m| m.start() == 0 && rs + m.end() == re_end)
}

/// Whether `pattern` has a `{` that does NOT open a STRICT `{n}` / `{n,}` / `{n,m}` (ASCII digits only, no
/// whitespace). Such a brace is read differently, legally, by the two engines: CPython (and REAL) treat a
/// malformed `{...}` as a literal, while the regex crate is whitespace-tolerant and may read `{ 4 }` / `{\n4\n}`
/// as a quantifier — `${\n…}` then becomes `$` repeated (an empty match at the end) where re/REAL find nothing.
/// A parser-interpretation divergence, not a bug; here the FORM is the right filter (unlike the leftmost class).
fn has_non_strict_brace(pattern: &str) -> bool {
    let b = pattern.as_bytes();
    let mut i = 0;
    let mut in_class = false;
    let mut escaped = false;
    while i < b.len() {
        let c = b[i];
        if escaped {
            escaped = false;
            i += 1;
        } else if c == b'\\' {
            escaped = true;
            i += 1;
        } else if in_class {
            if c == b']' {
                in_class = false;
            }
            i += 1;
        } else if c == b'[' {
            in_class = true;
            i += 1;
        } else if c == b'{' {
            let mut j = i + 1;
            let digits_start = j;
            while j < b.len() && b[j].is_ascii_digit() {
                j += 1;
            }
            let mut ok = j > digits_start; // at least one leading digit
            if ok && j < b.len() && b[j] == b',' {
                j += 1;
                while j < b.len() && b[j].is_ascii_digit() {
                    j += 1;
                }
            }
            ok = ok && j < b.len() && b[j] == b'}';
            if !ok {
                return true; // not a strict quantifier brace -> interpretation-divergent
            }
            i = j + 1;
        } else {
            i += 1;
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

    // The upstream regex-crate leftmost-first bug class (rust-lang/regex #1345/#1373) is NO LONGER skipped by
    // pattern shape — the form predicates were a losing whack-a-mole (top-level `|`, empty-branch `(|`, ...; the
    // real class is "any alternation where the crate's meta-engine picks its reverse-suffix search"). It is now
    // caught SEMANTICALLY at the find_iter comparison below: if a divergent span is one REAL found that the crate
    // dropped, yet the crate ITSELF confirms that span when anchored (`^(?:pat)`), the crate skipped a match it
    // agrees exists — its bug, logged and skipped. So alternation is fully differentiated again. See
    // fuzz/known_rust_bugs/.

    // A `{` that is not a strict {n}/{n,}/{n,m} is read literally by CPython/REAL but as a whitespace-tolerant
    // quantifier by the crate — a legal parser-interpretation divergence (known_rust_bugs / divergences). Form is
    // the right filter here (unlike the leftmost class, handled semantically at the comparison).
    if has_non_strict_brace(pattern) {
        return;
    }
    // Skip the documented CPython-vs-UTS#18 word/space divergence (README "Divergences"): REAL's \w/\s follow
    // Python re, the regex crate follows UTS#18, and they disagree on a fixed set of code points. When a
    // \w/\W/\s/\S/\b/\B/\</\> pattern meets a text carrying one, the two legitimately differ — not a REAL bug (the
    // REAL/re differential and the 3.2M-case exhaustive assert REAL's side).
    if uses_word_or_space_class(pattern) && text.chars().any(|c| WORD_SPACE_DELTAS.contains(&c)) {
        return;
    }
    // Skip the documented CPython-vs-Unicode-CaseFolding icase divergence: under (?i) REAL folds the Turkish
    // dotless/dotted I (U+0131/U+0130) with I/i — Python `re`'s equivalence via upper/lower — while the regex
    // crate keeps them apart (simple CaseFolding). When an icase pattern meets one of those code points the two
    // legitimately differ, both correct for their contract. \p{} itself is now fully compared (both Unicode
    // 16.0.0; the exhaustive per-property proof is tests/property_differential.rs).
    if is_icase(pattern) && text.chars().any(|c| ICASE_FOLD_DELTAS.contains(&c)) {
        return;
    }

    let re = match real_regex::Regex::new(pattern) {
        Ok(r) => r,
        Err(_) => return, // real rejects (invalid, or unsupported: a lookaround, a backref, a binary \p property)
    };
    let std = match regex::Regex::new(pattern) {
        Ok(r) => r,
        Err(_) => return, // regex rejects
    };

    let ours: Vec<_> = re.find_iter(text).map(|m| (m.start(), m.end())).collect();
    let theirs: Vec<_> = std.find_iter(text).map(|m| (m.start(), m.end())).collect();
    if ours != theirs {
        if crate_dropped_a_leftmost_it_confirms(pattern, text, &ours, &theirs) {
            // The crate's #1345 leftmost bug — not a REAL error. Logged (its count is the wake signal: a merged
            // upstream fix should drive it to zero), and skip the rest (captures/replace/split would differ the
            // same way, downstream of the same search).
            eprintln!("UPSTREAM-#1345 skip: crate dropped a leftmost it confirms anchored; \
                       pat={pattern:?} text={text:?} real={ours:?} crate={theirs:?}");
            return;
        }
        panic!("find_iter spans differ; pat={pattern:?} text={text:?} real={ours:?} crate={theirs:?}");
    }

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
