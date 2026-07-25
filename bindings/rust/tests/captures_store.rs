//! Captures' slot storage: the inline/spill boundary, unset groups, and independence from the
//! iterator's reused engine buffer.
//!
//! Captures keeps its slots inline (flat `[start0, end0, …]`, `usize::MAX` = did not participate) up to
//! a fixed capacity and spills to the heap beyond it, so `captures_iter` allocates nothing per match on
//! the group counts that dominate. These tests pin the observable behaviour on both sides of that
//! boundary — the capacity is an internal constant, so they walk a range that provably crosses it.
use real_regex::Regex;

// Build `(\w)` repeated n times: n groups + group 0 = 2 * (n + 1) slots.
fn nth_group_pattern(n: usize) -> String {
    r"(\w)".repeat(n)
}

#[test]
fn every_group_readable_across_the_inline_spill_boundary() {
    let alphabet = "abcdefghijklmnop";
    for n in 1..=12 {
        let re = Regex::new(&nth_group_pattern(n)).unwrap();
        let text = &alphabet[..n];
        let caps = re.captures(text).unwrap_or_else(|| panic!("no match for n={n}"));

        assert_eq!(caps.len(), n + 1, "group count for n={n}");
        assert_eq!(&caps[0], text, "whole match for n={n}");
        for g in 1..=n {
            let m = caps.get(g).unwrap_or_else(|| panic!("group {g} unset for n={n}"));
            assert_eq!(m.start(), g - 1, "group {g} start for n={n}");
            assert_eq!(m.end(), g, "group {g} end for n={n}");
            assert_eq!(m.as_str(), &text[g - 1..g], "group {g} text for n={n}");
        }
        assert!(caps.get(n + 1).is_none(), "one past the last group must be None for n={n}");
        assert!(caps.get(usize::MAX).is_none(), "an absurd index must be None, not panic");
        assert!(!caps.is_empty());

        let via_iter: Vec<Option<(usize, usize)>> =
            caps.iter().map(|o| o.map(|m| (m.start(), m.end()))).collect();
        assert_eq!(via_iter.len(), n + 1, "iter() length for n={n}");
        for g in 0..=n {
            assert_eq!(via_iter[g], caps.get(g).map(|m| (m.start(), m.end())), "iter()[{g}] for n={n}");
        }
    }
}

#[test]
fn unset_groups_stay_none_when_spilled() {
    // Seven groups (16 slots) with four optional ones: well past the inline capacity, and the
    // did-not-participate sentinel must survive the spill rather than read as offset usize::MAX.
    let re = Regex::new(r"(a)(b)?(c)(d)?(e)(f)?(g)").unwrap();

    let none = re.captures("aceg").unwrap();
    assert_eq!(&none[0], "aceg");
    for g in [2, 4, 6] {
        assert!(none.get(g).is_none(), "optional group {g} must be None when absent");
    }
    for g in [1, 3, 5, 7] {
        assert!(none.get(g).is_some(), "mandatory group {g} must participate");
    }

    let all = re.captures("abcdefg").unwrap();
    assert_eq!(&all[0], "abcdefg");
    for g in 1..=7 {
        assert!(all.get(g).is_some(), "group {g} must participate on the full subject");
    }
    assert_eq!(&all[2], "b");
    assert_eq!(&all[6], "f");
}

#[test]
fn named_lookup_works_on_both_sides_of_the_boundary() {
    let re = Regex::new(r"(?P<a>\w)(?P<b>\w)").unwrap(); // inline
    let caps = re.captures("xy").unwrap();
    assert_eq!(&caps["a"], "x");
    assert_eq!(&caps["b"], "y");
    assert!(caps.name("nope").is_none());

    let re = Regex::new(r"(?P<a>\w)(?P<b>\w)(?P<c>\w)(?P<d>\w)(?P<e>\w)(?P<f>\w)").unwrap(); // spilled
    let caps = re.captures("uvwxyz").unwrap();
    for (name, want) in [("a", "u"), ("b", "v"), ("c", "w"), ("d", "x"), ("e", "y"), ("f", "z")] {
        assert_eq!(&caps[name], want, "named group {name}");
    }
    assert!(caps.name("nope").is_none());
}

#[test]
fn each_captures_is_independent_of_the_reused_engine_buffer() {
    // captures_iter refills ONE flat engine buffer per match; each Captures must copy out of it. If it
    // borrowed instead, every collected Captures would read as the last match. Checked on both sides of
    // the inline boundary (2 groups inline, 6 groups spilled).
    for pat in [r"(\w)(\w)", r"(\w)(\w)(\w)(\w)(\w)(\w)"] {
        let re = Regex::new(pat).unwrap();
        let text = "abcdefghijkl mnopqrstuvwx";
        let collected: Vec<_> = re.captures_iter(text).collect();
        let streamed: Vec<Vec<Option<(usize, usize)>>> = re
            .captures_iter(text)
            .map(|c| c.iter().map(|o| o.map(|m| (m.start(), m.end()))).collect())
            .collect();
        assert!(collected.len() > 1, "need several matches to catch aliasing for {pat}");
        assert_eq!(collected.len(), streamed.len());
        for (i, caps) in collected.iter().enumerate() {
            let held: Vec<Option<(usize, usize)>> =
                caps.iter().map(|o| o.map(|m| (m.start(), m.end()))).collect();
            assert_eq!(held, streamed[i], "collected Captures {i} drifted for {pat}");
        }
        // Distinct matches really are distinct (guards a test that would pass on all-equal spans).
        assert_ne!(streamed[0], streamed[1], "matches must differ for {pat}");
    }
}

#[test]
fn bytes_captures_cross_the_boundary_too() {
    use real_regex::bytes::Regex as BRegex;

    let re = BRegex::new(r"(\w)(\w)").unwrap(); // inline
    let caps = re.captures(b"hi").unwrap();
    assert_eq!(caps.len(), 3);
    assert_eq!(&caps[1], b"h");
    assert_eq!(&caps[2], b"i");

    let re = BRegex::new(r"(\w)(\w)(\w)(\w)(\w)(\w)").unwrap(); // spilled
    let caps = re.captures(b"abcdef").unwrap();
    assert_eq!(caps.len(), 7);
    for (g, want) in (1..=6).zip([b"a", b"b", b"c", b"d", b"e", b"f"]) {
        assert_eq!(&caps[g], want, "bytes group {g}");
    }
    assert!(caps.get(7).is_none());

    let re = BRegex::new(r"(a)(b)?(c)(d)?(e)(f)?(g)").unwrap(); // spilled, with unset groups
    let caps = re.captures(b"aceg").unwrap();
    for g in [2, 4, 6] {
        assert!(caps.get(g).is_none(), "bytes optional group {g} must be None");
    }
    assert_eq!(&caps[0], b"aceg");
}
