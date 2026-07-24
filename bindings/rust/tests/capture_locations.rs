//! capture_locations + captures_read: reusable buffer, parity with captures, multi-text reuse.
use real_regex::Regex;

#[test]
fn captures_read_matches_captures() {
    let re = Regex::new(r"(?P<y>\d{4})-(?P<m>\d{2})").unwrap();
    let text = "x 2026-07 y 1999-12 z";
    let caps = re.captures(text).unwrap();
    let mut locs = re.capture_locations();
    let m = re.captures_read(&mut locs, text).unwrap();
    assert_eq!(m.as_str(), &caps[0]);
    assert_eq!(
        locs.get(0),
        Some((caps.get(0).unwrap().start(), caps.get(0).unwrap().end()))
    );
    assert_eq!(
        locs.get(1),
        Some((caps.get(1).unwrap().start(), caps.get(1).unwrap().end()))
    );
    assert_eq!(
        locs.get(2),
        Some((caps.get(2).unwrap().start(), caps.get(2).unwrap().end()))
    );
    assert_eq!(locs.len(), re.captures_len());
}

#[test]
fn captures_read_at_and_reuse_across_texts() {
    let re = Regex::new(r"(\w+)@(\w+)").unwrap();
    let mut locs = re.capture_locations();
    assert_eq!(locs.len(), 3);

    let m1 = re.captures_read(&mut locs, "a@b c@d").unwrap();
    assert_eq!(m1.as_str(), "a@b");
    assert_eq!(locs.get(1), Some((0, 1)));
    assert_eq!(locs.get(2), Some((2, 3)));

    // Reuse on another subject without allocating a Captures.
    assert!(re.captures_read(&mut locs, "nope").is_none());
    let m3 = re.captures_read_at(&mut locs, "xx yy@zz", 3).unwrap();
    assert_eq!(m3.as_str(), "yy@zz");
    assert_eq!(locs.get(1).map(|(a, b)| &"xx yy@zz"[a..b]), Some("yy"));
    assert_eq!(locs.get(2).map(|(a, b)| &"xx yy@zz"[a..b]), Some("zz"));
}

#[test]
fn captures_read_iter_parity_vs_captures_iter() {
    let re = Regex::new(r"(\d+)-(\d+)").unwrap();
    let text = "a 1-2 b 34-5 c 6-78";
    let via_iter: Vec<Vec<_>> = re
        .captures_iter(text)
        .map(|c| (0..c.len()).map(|g| c.get(g).map(|m| (m.start(), m.end()))).collect())
        .collect();

    let mut via_read = Vec::new();
    let mut it = re.captures_read_iter(text);
    while let Some(_m) = it.next() {
        via_read.push((0..it.len()).map(|g| it.get(g)).collect::<Vec<_>>());
    }
    assert_eq!(via_read, via_iter);

    // read_captures copies the last match into a reusable CaptureLocations.
    let mut it2 = re.captures_read_iter(text);
    let mut locs = re.capture_locations();
    let m = it2.next().unwrap();
    it2.read_captures(&mut locs);
    assert_eq!(locs.get(0), Some((m.start(), m.end())));
}

#[test]
fn bytes_captures_read_parity() {
    use real_regex::bytes;
    let re = bytes::Regex::new(r"(\w+)=(\w+)").unwrap();
    let text = b"key=val other=x";
    let caps = re.captures(text).unwrap();
    let mut locs = re.capture_locations();
    let m = re.captures_read(&mut locs, text).unwrap();
    assert_eq!(m.as_bytes(), b"key=val");
    assert_eq!(locs.get(1), Some((0, 3)));
    assert_eq!(locs.get(2), Some((4, 7)));
    assert_eq!(
        locs.get(0),
        Some((caps.get(0).unwrap().start(), caps.get(0).unwrap().end()))
    );
}
