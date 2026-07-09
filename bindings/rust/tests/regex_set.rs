//! RegexSet which-matched (Stage-1 N-walks).
use real_regex::RegexSet;

#[test]
fn which_matched_order_and_any() {
    let set = RegexSet::new(["alpha", "beta", "gamma"]).unwrap();
    assert_eq!(set.len(), 3);
    assert_eq!(set.matches("xx beta yy"), vec![false, true, false]);
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
        assert_eq!(hit[i], alone, "pattern {i}");
    }
}

#[test]
fn compile_fail_no_silent_skip() {
    assert!(RegexSet::new(["ok", "(?>atomic)"]).is_err());
}

#[test]
fn both_patterns_can_match_same_text() {
    // which-matched reports BOTH; not maximal-munch one-winner (dfa).
    let set = RegexSet::new(["ab", "a"]).unwrap();
    assert_eq!(set.matches("ab"), vec![true, true]);
}
