// The landing page's Rust quickstart tab, built and run by `rust-test`
// (`cargo run --example quickstart`) against the crate so the code the page shows is
// exactly the code that's tested, never merely illustrative. A pair of region-boundary
// comments further down bounds the
// block conf.py's `_inject_quickstart` (html-page-context hook) reads and Pygments-highlights
// onto the page at build time (NOTE: keep this comment from ever spelling out that pair of
// comments literally — the hook's region search is a first-occurrence substring search,
// and a mention up here would shadow the real one below) — do not edit the marked lines
// without also checking the
// landing's Rust tab still matches byte-for-byte (rebuild with `make docs-site` and diff).
use std::error::Error;

// The marked region below is reproduced byte-for-byte on the landing page's Rust tab (not
// editable here — see the header comment above), including its unused `&caps["host"];` display
// statement; that line is what the landing shows, and it is intentionally harmless (no side
// effect), just noisy under `unused_must_use`. Silenced at the function, not inside the region.
#[allow(unused_must_use)]
fn main() -> Result<(), Box<dyn Error>> {
    // [quickstart]
    use real_regex::Regex;   // drop-in for the regex crate

    let re = Regex::new(r"(?P<user>\w+)@(?P<host>\w+)")?;
    let caps = re.captures("info@example.com").unwrap();
    &caps["host"];              // "example"
    // [/quickstart]

    assert_eq!(&caps["host"], "example");
    Ok(())
}
