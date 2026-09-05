//! The `regex` crate as the oracle for the whole `Regex` surface, not for four methods of it.
//!
//! `differential.rs` crosses a table of twenty (pattern, text) pairs with `find_iter`,
//! `captures_iter`, `find_at` and a few starts. This crate exposes some forty public methods, and
//! the ones that were never asked of the oracle are exactly the ones that carry their own logic:
//! `split`/`splitn`, `replace`/`replace_all`/`replacen`, `captures_read`/`capture_locations`, the
//! `_at` variants, and the whole `bytes` half — which was never checked against the `str` half
//! either, though the two are the same question asked twice.
//!
//! Deterministic cross-product: no seed to record, a divergence is reproducible by construction.
//!
//! One divergence is routed around and pinned rather than left to fire here: `shortest_match`,
//! where this crate returns the leftmost match's greedy end and `regex` the earliest position at
//! which a match completes. Routing a difference around a sweep without asserting it elsewhere is
//! how documentation goes stale unnoticed, so it is asserted, in both directions.
//!
//! Empty-alternation-branch repetitions are NOT routed around, though the engine's own sequence for
//! them differs from the crate's: this crate's iterator drives the search by position after the
//! first empty match precisely to reproduce the crate's advancement, so the crate is an oracle for
//! them here even though it is not one for the engine. Excluding them would have left that
//! mechanism untested on the only forms that exercise it.

use real_regex::Regex;

/// Chosen for the surfaces above rather than for the engine: empty-matchable patterns (where an
/// iterator's advancement rule shows), capturing shapes (where `split`'s and `replace`'s group
/// handling shows), non-participating groups, and non-ASCII (where the crate and this engine are
/// both Unicode-aware and must therefore agree, unlike Go's RE2).
const PATTERNS: &[&str] = &[
    "a", "ab", "[ab]+", "a|b", "abc", r"\w+", r"\d", r"\s*", r"\b", r"\B",
    "a*", "x*", "a?", "a*?", "a??", "a+?", "[a]*", "a{2}", "a{1,3}",
    "(a)", "(a)(b)", "(a)|(b)", "(a(b))", "(?P<x>a)", "(?P<x>a)(?P<y>b)",
    "(a)*", "(a)?", "(?:a)(b)", "(a)|b", "a|(b)", "(x*)",
    "^a", "a$", "^", "$", "^a$",
    ".", "(.)", "(.)(.)", "[^a]", ",", "é", "é+", "(é)", "[é]", "😀", "[à-ÿ]+",
    // Empty alternation branches, INCLUDING under a repetition. The engine's own sequence for these
    // differs from the crate's (README: div_empty_first_branch_loop), but this crate's iterator
    // drives the search by position after the first empty match precisely to reproduce the crate's
    // rule -- so the crate is an oracle for them HERE even though it is not one for the engine.
    // Routing them out would leave that mechanism untested on the only forms that exercise it.
    "(|a)", "(a|)", "(|a)*", "(a|)*", "(|a)+", "(?:|ab)*", "(a||b)*", "(a|)b", "(|a)?",
];

const SUBJECTS: &[&str] = &[
    "", "a", "b", "ab", "ba", "aab", "abc", "aaa", "abab", "banana",
    "axbxc", "a,b,,c,", "a b  c", "xaybz", "a\nb", "  a  ", "\n",
    "é", "ée", "aéb", "a😀b", "Кот", "café",
];

/// `$0`, a named reference, a literal `$$`, and an out-of-range `$9` — this crate uses the crate's
/// own template spelling, so the templates need no translation and a mismatch is a real difference.
const TEMPLATES: &[&str] = &["-", "", "<>", "[$0]", "$1", "${x}", "$$", "x${1}y", "$9"];

const LIMITS: &[usize] = &[0, 1, 2, 3];

struct Report {
    compared: usize,
    skipped: usize,
    fails: Vec<String>,
}

impl Report {
    fn new() -> Self {
        Report { compared: 0, skipped: 0, fails: Vec::new() }
    }

    /// `rhs` names what the second value IS. Hard-coding "regex" made the report lie the moment a
    /// comparison was against something else -- the bytes/str duality read as a crate divergence
    /// when both sides were this crate.
    fn eq<T: PartialEq + std::fmt::Debug>(
        &mut self,
        surface: &str,
        pattern: &str,
        subject: &str,
        extra: &str,
        ours: T,
        theirs: T,
        rhs: &str,
    ) {
        self.compared += 1;
        if ours != theirs {
            self.fails.push(format!(
                "{surface}(pattern={pattern:?}, subject={subject:?}{extra}): \
                 this crate {ours:?}, {rhs} {theirs:?}"
            ));
        }
    }

    fn finish(self, label: &str) {
        println!("{label}: {} comparisons, {} skipped", self.compared, self.skipped);
        assert!(self.compared > 0, "{label}: nothing was compared");
        if self.fails.is_empty() {
            return;
        }
        // The tally is untruncated and the examples are capped: a list of examples answers "what
        // does one look like", only the tally answers "is this one family or several".
        let mut by_surface: Vec<(String, usize)> = Vec::new();
        for f in &self.fails {
            let head = f.split('(').next().unwrap_or("?").to_string();
            match by_surface.iter_mut().find(|(s, _)| *s == head) {
                Some((_, n)) => *n += 1,
                None => by_surface.push((head, 1)),
            }
        }
        by_surface.sort_by(|a, b| b.1.cmp(&a.1));
        let tally: Vec<String> =
            by_surface.iter().map(|(s, n)| format!("{s}×{n}")).collect();
        let shown: Vec<&String> = self.fails.iter().take(10).collect();
        panic!(
            "{label}: {} divergence(s) across {} surface(s): {}\n  {}\n  ... {} more",
            self.fails.len(),
            by_surface.len(),
            tally.join(" "),
            shown.iter().map(|s| s.as_str()).collect::<Vec<_>>().join("\n  "),
            self.fails.len().saturating_sub(shown.len())
        );
    }
}

fn oracle_pairs() -> Vec<(&'static str, Regex, regex::Regex)> {
    let mut out = Vec::new();
    for &pat in PATTERNS {
        let theirs = match regex::Regex::new(pat) {
            Ok(r) => r,
            Err(_) => continue, // no oracle answer
        };
        let ours = Regex::new(pat).unwrap_or_else(|e| {
            panic!("regex accepts {pat:?} and this crate does not: {e}")
        });
        out.push((pat, ours, theirs));
    }
    out
}

#[test]
fn find_family_matches_the_regex_crate() {
    let mut r = Report::new();
    for (pat, ours, theirs) in oracle_pairs() {
        for &subj in SUBJECTS {
            r.eq("is_match", pat, subj, "", ours.is_match(subj), theirs.is_match(subj), "regex");
            r.eq(
                "find",
                pat,
                subj,
                "",
                ours.find(subj).map(|m| (m.start(), m.end())),
                theirs.find(subj).map(|m| (m.start(), m.end())),
                "regex",
            );
            r.eq(
                "find_iter",
                pat,
                subj,
                "",
                ours.find_iter(subj).map(|m| (m.start(), m.end())).collect::<Vec<_>>(),
                theirs.find_iter(subj).map(|m| (m.start(), m.end())).collect::<Vec<_>>(),
                "regex",
            );
            r.eq(
                "captures",
                pat,
                subj,
                "",
                ours.captures(subj).map(|c| group_spans(c.len(), |i| c.get(i).map(|m| (m.start(), m.end())))),
                theirs.captures(subj).map(|c| group_spans(c.len(), |i| c.get(i).map(|m| (m.start(), m.end())))),
                "regex",
            );
            r.eq(
                "captures_iter",
                pat,
                subj,
                "",
                ours.captures_iter(subj)
                    .map(|c| group_spans(c.len(), |i| c.get(i).map(|m| (m.start(), m.end()))))
                    .collect::<Vec<_>>(),
                theirs
                    .captures_iter(subj)
                    .map(|c| group_spans(c.len(), |i| c.get(i).map(|m| (m.start(), m.end()))))
                    .collect::<Vec<_>>(),
                "regex",
            );

            // The `_at` variants: a start offset must be a char boundary, so only those are asked.
            for start in [0usize, 1, 2, subj.len()] {
                if start > subj.len() || !subj.is_char_boundary(start) {
                    r.skipped += 1;
                    continue;
                }
                let extra = format!(", start={start}");
                r.eq("is_match_at", pat, subj, &extra, ours.is_match_at(subj, start), theirs.is_match_at(subj, start), "regex");
                r.eq(
                    "find_at",
                    pat,
                    subj,
                    &extra,
                    ours.find_at(subj, start).map(|m| (m.start(), m.end())),
                    theirs.find_at(subj, start).map(|m| (m.start(), m.end())),
                    "regex",
                );
                r.eq(
                    "captures_at",
                    pat,
                    subj,
                    &extra,
                    ours.captures_at(subj, start).map(|c| group_spans(c.len(), |i| c.get(i).map(|m| (m.start(), m.end())))),
                    theirs.captures_at(subj, start).map(|c| group_spans(c.len(), |i| c.get(i).map(|m| (m.start(), m.end())))),
                    "regex",
                );
            }
        }
    }
    r.finish("find family");
}

fn group_spans<F: Fn(usize) -> Option<(usize, usize)>>(len: usize, get: F) -> Vec<Option<(usize, usize)>> {
    (0..len).map(get).collect()
}

#[test]
fn split_and_replace_match_the_regex_crate() {
    let mut r = Report::new();
    for (pat, ours, theirs) in oracle_pairs() {
        for &subj in SUBJECTS {
            r.eq(
                "split",
                pat,
                subj,
                "",
                ours.split(subj).collect::<Vec<_>>(),
                theirs.split(subj).collect::<Vec<_>>(),
                "regex",
            );
            for &limit in LIMITS {
                r.eq(
                    "splitn",
                    pat,
                    subj,
                    &format!(", limit={limit}"),
                    ours.splitn(subj, limit).collect::<Vec<_>>(),
                    theirs.splitn(subj, limit).collect::<Vec<_>>(),
                    "regex",
                );
            }
            for &tmpl in TEMPLATES {
                let extra = format!(", template={tmpl:?}");
                r.eq("replace", pat, subj, &extra,
                     ours.replace(subj, tmpl).to_string(), theirs.replace(subj, tmpl).to_string(), "regex");
                r.eq("replace_all", pat, subj, &extra,
                     ours.replace_all(subj, tmpl).to_string(), theirs.replace_all(subj, tmpl).to_string(),
                     "regex");
                for &limit in LIMITS {
                    r.eq(
                        "replacen",
                        pat,
                        subj,
                        &format!("{extra}, limit={limit}"),
                        ours.replacen(subj, limit, tmpl).to_string(),
                        theirs.replacen(subj, limit, tmpl).to_string(),
                        "regex",
                    );
                }
            }
        }
    }
    r.finish("split/replace");
}

#[test]
fn capture_locations_agree_with_captures() {
    // No oracle needed for the internal half: `captures_read` writes into a CaptureLocations and
    // `captures` builds a Captures, from the same match. They must report the same spans whatever
    // those spans are, and the crate is asked the same question so a three-way disagreement is
    // attributable.
    let mut r = Report::new();
    for (pat, ours, theirs) in oracle_pairs() {
        for &subj in SUBJECTS {
            let mut locs = ours.capture_locations();
            let read = ours.captures_read(&mut locs, subj).map(|m| (m.start(), m.end()));
            let via_captures = ours.captures(subj).map(|c| {
                let m = c.get(0).unwrap();
                (m.start(), m.end())
            });
            r.eq("captures_read vs captures", pat, subj, "", read, via_captures, "captures()");

            if read.is_some() {
                let from_locs: Vec<Option<(usize, usize)>> =
                    (0..locs.len()).map(|i| locs.get(i)).collect();
                let from_caps: Vec<Option<(usize, usize)>> = ours
                    .captures(subj)
                    .map(|c| group_spans(c.len(), |i| c.get(i).map(|m| (m.start(), m.end()))))
                    .unwrap_or_default();
                r.eq("locations vs captures groups", pat, subj, "", from_locs, from_caps, "captures()");
            }

            let mut their_locs = theirs.capture_locations();
            r.eq(
                "captures_read span",
                pat,
                subj,
                "",
                read,
                theirs.captures_read(&mut their_locs, subj).map(|m| (m.start(), m.end())),
                "regex",
            );
        }
    }
    r.finish("capture locations");
}

#[test]
fn bytes_half_matches_the_regex_crate_in_byte_mode() {
    // The bytes module's own documentation states the rule: patterns compile in REAL's raw-byte
    // mode, where `\w \d \s \b` are ASCII and `.` is one BYTE. `regex::bytes::Regex` is
    // Unicode-aware by default, so it is not an oracle as written -- but `(?-u)` puts it in exactly
    // that byte mode, which makes it one. Comparing the str half against the bytes half instead
    // would only assert that two deliberately different semantics are the same.
    let mut r = Report::new();
    for &pat in PATTERNS {
        let ours = match real_regex::bytes::Regex::new(pat) {
            Ok(b) => b,
            Err(_) => {
                r.skipped += 1;
                continue;
            }
        };
        let theirs = match regex::bytes::Regex::new(&format!("(?-u:{pat})")) {
            Ok(b) => b,
            Err(_) => {
                r.skipped += 1; // no oracle answer in byte mode (a non-ASCII literal, typically)
                continue;
            }
        };
        for &subj in SUBJECTS {
            let b = subj.as_bytes();
            r.eq("is_match", pat, subj, " (bytes)", ours.is_match(b), theirs.is_match(b), "regex::bytes");
            r.eq(
                "find",
                pat,
                subj,
                " (bytes)",
                ours.find(b).map(|m| (m.start(), m.end())),
                theirs.find(b).map(|m| (m.start(), m.end())),
                "regex::bytes",
            );
            r.eq(
                "find_iter",
                pat,
                subj,
                " (bytes)",
                ours.find_iter(b).map(|m| (m.start(), m.end())).collect::<Vec<_>>(),
                theirs.find_iter(b).map(|m| (m.start(), m.end())).collect::<Vec<_>>(),
                "regex::bytes",
            );
            r.eq(
                "split",
                pat,
                subj,
                " (bytes)",
                ours.split(b).map(|s| s.to_vec()).collect::<Vec<_>>(),
                theirs.split(b).map(|s| s.to_vec()).collect::<Vec<_>>(),
                "regex::bytes",
            );
        }
        // Bytes that no `&str` can carry: this half exists for them, so they belong here and not
        // only in the str-shaped subject list.
        for raw in [&b""[..], &[0xff][..], &[0x80, 0x80][..], b"a\xffb", &[0xe2, 0x82][..]] {
            let label = String::from_utf8_lossy(raw).into_owned();
            r.eq(
                "find/raw",
                pat,
                &label,
                " (bytes)",
                ours.find(raw).map(|m| (m.start(), m.end())),
                theirs.find(raw).map(|m| (m.start(), m.end())),
                "regex::bytes",
            );
            r.eq(
                "find_iter/raw",
                pat,
                &label,
                " (bytes)",
                ours.find_iter(raw).map(|m| (m.start(), m.end())).collect::<Vec<_>>(),
                theirs.find_iter(raw).map(|m| (m.start(), m.end())).collect::<Vec<_>>(),
                "regex::bytes",
            );
        }
    }
    r.finish("bytes half vs regex::bytes");
}

#[test]
fn documented_shortest_match_divergence_still_holds() {
    // README: this crate returns the leftmost match's GREEDY end, the crate the earliest position at
    // which a match completes. Asserted in both directions so that the day the engine grows a
    // first-accept stop, this fails instead of leaving the README asserting something false.
    let cases: &[(&str, &str, usize, usize)] = &[
        ("a+", "aaa", 3, 1),
        ("a+", "aaab", 3, 1),
        ("[ab]+", "abab", 4, 1),
        (r"\w+", "abc", 3, 1),
    ];
    for &(pat, text, want_ours, want_theirs) in cases {
        let ours = Regex::new(pat).unwrap().shortest_match(text);
        let theirs = regex::Regex::new(pat).unwrap().shortest_match(text);
        assert_eq!(ours, Some(want_ours), "{pat:?} on {text:?}: this crate");
        assert_eq!(theirs, Some(want_theirs), "{pat:?} on {text:?}: regex — the divergence has moved");
        assert_ne!(ours, theirs, "{pat:?} on {text:?}: the two agree; README says they differ");
    }
    assert_eq!(cases.len(), 4, "denominator changed");
}

#[test]
fn drive_mode_reproduces_the_crate_on_empty_branch_repetitions() {
    // The README's `div_empty_first_branch_loop` paragraph is about the ENGINE: on `(|a)*` over
    // "aa" the C++ engine and the Python binding both walk (0,0)(0,2)(2,2) while the crate walks
    // (0,0)(1,1)(2,2) and `re` walks five spans. That divergence is real and is pinned where the
    // engine lives.
    //
    // It does not reach a caller of THIS crate, and that is not luck: the iterator switches to
    // driving the search by position at the first empty match, precisely so rust's advancement rule
    // is reproduced rather than re's. These forms are the only ones that exercise that switch, so
    // asserting agreement on them is what tests the mechanism — and it is why the sweep above
    // includes them instead of routing them out. If drive mode regressed, this is what would say so.
    // Empty FIRST match, so the switch is reached. An empty alternation branch is not enough on its
    // own: `(a|)*` over "aa" takes the `a` branch and matches (0,2) greedily, never engaging drive
    // mode at all — including it here would have been a case that proves nothing, which is what the
    // second assertion below exists to catch.
    let cases: &[(&str, &str)] = &[
        ("(|a)*", "aa"), ("(|a)+", "aa"), ("(?:|ab)*", "abab"),
        ("(|a)*", "a"), ("(|a)", "aa"), ("(|a)?", "aa"), ("x*", "axbxc"), ("a*?", "aa"),
    ];
    for &(pat, text) in cases {
        let ours: Vec<(usize, usize)> =
            Regex::new(pat).unwrap().find_iter(text).map(|m| (m.start(), m.end())).collect();
        let theirs: Vec<(usize, usize)> =
            regex::Regex::new(pat).unwrap().find_iter(text).map(|m| (m.start(), m.end())).collect();
        assert_eq!(ours, theirs, "{pat:?} on {text:?}: drive mode no longer reproduces the crate");
    }

    // The switch has to be REACHED for the agreement above to mean anything: every case here must
    // begin with an empty match, which is what puts the iterator into drive mode in the first place.
    for &(pat, text) in cases {
        let first = Regex::new(pat).unwrap().find(text).map(|m| (m.start(), m.end()));
        let (s0, e0) = first.expect("case matches nothing, so it exercises nothing");
        assert_eq!(s0, e0, "{pat:?} on {text:?}: the first match is not empty, so drive mode never engages");
    }
    assert_eq!(cases.len(), 8, "denominator changed");

    // The forms whose empty branch does NOT produce an empty first match still have to agree; they
    // simply agree for a different reason, and saying so keeps the list above honest about what it
    // demonstrates.
    let greedy: &[(&str, &str)] = &[("(a|)*", "aa"), ("(a|)*", "ab"), ("(a||b)*", "ab")];
    for &(pat, text) in greedy {
        let ours: Vec<(usize, usize)> =
            Regex::new(pat).unwrap().find_iter(text).map(|m| (m.start(), m.end())).collect();
        let theirs: Vec<(usize, usize)> =
            regex::Regex::new(pat).unwrap().find_iter(text).map(|m| (m.start(), m.end())).collect();
        assert_eq!(ours, theirs, "{pat:?} on {text:?}");
        let (s0, e0) = Regex::new(pat).unwrap().find(text).map(|m| (m.start(), m.end())).unwrap();
        assert_ne!(s0, e0, "{pat:?} on {text:?} now starts empty; it belongs in the list above");
    }
    assert_eq!(greedy.len(), 3, "denominator changed");
}
