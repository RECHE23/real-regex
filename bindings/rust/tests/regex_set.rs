//! RegexSet which-matched (Stage-1 N-walks).
use real_regex::RegexSet;

#[test]
fn which_matched_order_and_any() {
    let set = RegexSet::new(["alpha", "beta", "gamma"]).unwrap();
    assert_eq!(set.len(), 3);
    assert_eq!(set.matches("xx beta yy").into_iter().collect::<Vec<_>>(), vec![1]);
    assert_eq!(set.matched_ids("xx beta yy"), vec![1]);
    assert!(set.is_match("please find beta"));
    assert!(!set.is_match("nothing"));
}

#[test]
fn oracle_n_search() {
    let pats = [
        r"[0-9]{4}-[0-9]{2}-[0-9]{2}",
        r"error|warn|info",
        r"[a-f0-9]{8}",
        r"absent_token_xyz",
    ];
    let text = "2026-06-13 error id=a3f9c1d8 GET /api\n";
    let set = RegexSet::new(pats).unwrap();
    let hit = set.matches(text);
    for (i, p) in pats.iter().enumerate() {
        let alone = real_regex::Regex::new(p).unwrap().is_match(text);
        assert_eq!(hit.matched(i), alone, "pattern {i}");
    }
}

#[test]
fn compile_fail_no_silent_skip() {
    // atomic groups: Tier 1 bodies only; a compound/alternating body is not.
    assert!(RegexSet::new(["ok", "(?>a|b)"]).is_err());
}

#[test]
fn both_patterns_can_match_same_text() {
    // which-matched reports BOTH; not maximal-munch one-winner (dfa).
    let set = RegexSet::new(["ab", "a"]).unwrap();
    assert_eq!(set.matches("ab").iter().collect::<Vec<_>>(), vec![0, 1]);
}

#[test]
fn debug_shows_the_patterns_like_the_regex_crate() {
    let ours = real_regex::RegexSet::new([r"a+", r"\d"]).unwrap();
    let theirs = regex::RegexSet::new([r"a+", r"\d"]).unwrap();
    assert_eq!(format!("{ours:?}"), format!("{theirs:?}"));
}

#[test]
fn set_matches_behaves_like_the_regex_crates() {
    let pattern_sets: [&[&str]; 5] = [
        &[],
        &["a"],
        &["a", "b", "c"],
        &[r"\d+", r"[a-z]+", r"^x", r"y$", "never_here"],
        &["ab", "a", "b", "", r"\s"],
    ];
    let subjects = ["", "a", "abc", "x1y", "no digits", "a b"];
    let mut compared = 0;
    for pats in pattern_sets {
        let ours = RegexSet::new(pats).unwrap();
        let theirs = regex::RegexSet::new(pats).unwrap();
        for subj in subjects {
            let (m, t) = (ours.matches(subj), theirs.matches(subj));
            let ctx = format!("{pats:?} on {subj:?}");
            assert_eq!(m.len(), t.len(), "{ctx}: len");
            assert_eq!(m.matched_any(), t.matched_any(), "{ctx}: matched_any");
            assert_eq!(m.matched_all(), t.matched_all(), "{ctx}: matched_all");
            for i in 0..t.len() {
                assert_eq!(m.matched(i), t.matched(i), "{ctx}: matched({i})");
            }
            assert_eq!(m.iter().collect::<Vec<_>>(), t.iter().collect::<Vec<_>>(), "{ctx}: iter");
            assert_eq!(m.iter().rev().collect::<Vec<_>>(), t.iter().rev().collect::<Vec<_>>(), "{ctx}: iter().rev()");
            assert_eq!((&m).into_iter().collect::<Vec<_>>(), (&t).into_iter().collect::<Vec<_>>(), "{ctx}: &into_iter");
            assert_eq!(m.clone().into_iter().rev().collect::<Vec<_>>(), t.clone().into_iter().rev().collect::<Vec<_>>(), "{ctx}: into_iter().rev()");
            let mut front_back = m.iter();
            let mut theirs_fb = t.iter();
            assert_eq!((front_back.next(), front_back.next_back()), (theirs_fb.next(), theirs_fb.next_back()), "{ctx}: mixed ends");
            assert_eq!(ours.matched_ids(subj), t.iter().collect::<Vec<_>>(), "{ctx}: matched_ids");
            compared += 1;
        }
    }
    assert_eq!(compared, 30, "denominator");
}

#[test]
#[should_panic]
fn matched_past_the_end_panics_like_the_regex_crate() {
    RegexSet::new(["a"]).unwrap().matches("a").matched(1);
}

#[test]
fn empty_and_default_are_the_regex_crates() {
    for set in [RegexSet::empty(), RegexSet::default()] {
        assert!(set.is_empty());
        assert_eq!(set.len(), 0);
        assert!(!set.is_match("anything"));
        let m = set.matches("anything");
        assert!(!m.matched_any() && m.matched_all());
        assert_eq!(m.len(), 0);
        assert_eq!(m.matched_all(), regex::RegexSet::empty().matches("anything").matched_all());
    }
}
