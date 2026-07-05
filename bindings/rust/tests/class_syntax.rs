//! RD.3-fix-v4: rust-only character-class syntax. The `regex` crate supports nested classes ([a[b]] = union)
//! and set operators (&&, --, ~~); Python re (REAL's model) treats [ as a literal inside a class, so the two
//! parse [a[b]] differently. The crate declines these up front (Unsupported), so it never silently
//! mis-matches; escaped forms that re and rust agree on stay accepted.
use real_regex::Regex;

#[test]
fn declines_rust_only_class_syntax() {
    for pat in [r"[a[b]]", r"[[abc]]", r"[a&&b]", r"[a--b]", r"[a~~b]", r"[0[0]\x05]"] {
        let err = Regex::new(pat).expect_err(pat);
        assert!(err.is_unsupported(), "{pat:?} should be Unsupported, got {err:?}");
    }
}

#[test]
fn accepts_escaped_and_plain_classes() {
    // Escaped [ / - / and ordinary classes: re and rust agree, so they must still compile.
    for pat in [r"[\[]", r"[a\-b]", r"[-a]", r"[a-]", r"[a-z]", r"[a-z0-9_]", r"[]a]", r"[^]]"] {
        assert!(Regex::new(pat).is_ok(), "{pat:?} should compile");
    }
}

#[test]
fn declines_in_bytes_api_too() {
    match real_regex::bytes::Regex::new(r"[a[b]]") {
        Err(e) => assert!(e.is_unsupported()),
        Ok(_) => panic!("[a[b]] should decline in the bytes API"),
    }
    assert!(real_regex::bytes::Regex::new(r"[\[]").is_ok());
}
