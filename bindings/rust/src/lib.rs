//! Linear-time, ReDoS-safe regular expressions with **bounded lookarounds** — a drop-in-shaped Rust binding
//! to the REAL C++ engine (via its C ABI). The API mirrors the [`regex`](https://docs.rs/regex) crate; every
//! pattern that compiles matches in time linear in the input, with no backtracking and so no catastrophic
//! blow-up. The engine is **strict by design** — a construct it cannot run linearly (a backreference, an
//! unbounded lookaround) is rejected at [`Regex::new`], never silently made non-linear.
//!
//! ```
//! use real_regex::Regex;
//! let re = Regex::new(r"(?P<y>\d{4})-(?P<m>\d{2})").unwrap();
//! let caps = re.captures("2026-07").unwrap();
//! assert_eq!(&caps["y"], "2026");
//! assert_eq!(caps.get(2).unwrap().as_str(), "07");
//! ```
use std::collections::HashMap;
use std::marker::PhantomData;
use std::ops::Index;
use std::os::raw::c_char;
use std::sync::Arc;

// Opaque C handles.
enum RealRegex {}
enum RealIter {}

extern "C" {
    fn real_compile(pattern: *const c_char, len: usize, flags: u32,
                    errbuf: *mut c_char, errbuf_len: usize) -> *mut RealRegex;
    fn real_group_count(re: *const RealRegex) -> usize;
    fn real_group_name(re: *const RealRegex, group: usize, buf: *mut c_char, buflen: usize) -> usize;
    fn real_free(re: *mut RealRegex);
    fn real_find_iter(re: *const RealRegex, text: *const c_char, len: usize) -> *mut RealIter;
    fn real_find_iter_at(re: *const RealRegex, text: *const c_char, len: usize, start: usize) -> *mut RealIter;
    fn real_iter_next(iter: *mut RealIter, spans: *mut usize) -> i32;
    fn real_iter_free(iter: *mut RealIter);
}

// Group metadata, shared cheaply (Arc) by the Regex and every Captures it produces — this is what lets
// Captures carry a single lifetime, like the regex crate.
struct GroupInfo {
    names: Vec<Option<String>>,       // by group index (None = unnamed)
    by_name: HashMap<String, usize>,  // name -> group index
}

/// A compiled pattern.
pub struct Regex {
    handle: *mut RealRegex,
    ngroups: usize, // capture slots per match, including group 0
    pattern: String,
    groups: Arc<GroupInfo>,
}

// The handle is an owned heap object with no interior mutability observable from Rust; sharing a &Regex
// across threads (read-only matching) is sound.
unsafe impl Send for Regex {}
unsafe impl Sync for Regex {}

impl Regex {
    /// Compile `pattern`. Returns the engine's error message if the pattern is invalid or cannot be run
    /// linearly (the strict policy).
    pub fn new(pattern: &str) -> Result<Regex, String> {
        Regex::with_flags(pattern, 0)
    }

    /// Compile with a `real::flags` bitmask (icase=1, multiline=2, dotall=4, bytes=8, verbose=16, ecma=32,
    /// ascii=64). Prefer [`RegexBuilder`] for readable options.
    pub fn with_flags(pattern: &str, flags: u32) -> Result<Regex, String> {
        let mut err = [0u8; 256];
        let handle = unsafe {
            real_compile(pattern.as_ptr() as *const c_char, pattern.len(), flags,
                         err.as_mut_ptr() as *mut c_char, err.len())
        };
        if handle.is_null() {
            let end = err.iter().position(|&b| b == 0).unwrap_or(err.len());
            return Err(String::from_utf8_lossy(&err[..end]).into_owned());
        }
        let ngroups = unsafe { real_group_count(handle) };
        let mut names = Vec::with_capacity(ngroups);
        let mut by_name = HashMap::new();
        for g in 0..ngroups {
            let mut buf = [0u8; 128];
            let len = unsafe {
                real_group_name(handle, g, buf.as_mut_ptr() as *mut c_char, buf.len())
            };
            if len == 0 {
                names.push(None);
            } else {
                let n = len.min(buf.len() - 1);
                let name = String::from_utf8_lossy(&buf[..n]).into_owned();
                by_name.insert(name.clone(), g);
                names.push(Some(name));
            }
        }
        Ok(Regex {
            handle,
            ngroups,
            pattern: pattern.to_string(),
            groups: Arc::new(GroupInfo { names, by_name }),
        })
    }

    /// The original pattern string.
    pub fn as_str(&self) -> &str {
        &self.pattern
    }

    /// The number of capture slots, **including** the implicit whole-match group 0 (so always >= 1) —
    /// the regex crate's convention.
    pub fn captures_len(&self) -> usize {
        self.ngroups
    }

    /// The name of each capture group (group 0 first), `None` for the unnamed ones.
    pub fn capture_names(&self) -> impl Iterator<Item = Option<&str>> {
        self.groups.names.iter().map(|o| o.as_deref())
    }

    fn raw<'r, 't>(&'r self, text: &'t str, start: Option<usize>) -> RawSpans<'r, 't> {
        let iter = unsafe {
            match start {
                None => real_find_iter(self.handle, text.as_ptr() as *const c_char, text.len()),
                Some(s) => real_find_iter_at(self.handle, text.as_ptr() as *const c_char, text.len(), s),
            }
        };
        RawSpans { iter, ngroups: self.ngroups, _re: PhantomData }
    }

    fn caps_from<'t>(&self, text: &'t str, spans: Vec<Option<(usize, usize)>>) -> Captures<'t> {
        Captures { text, spans, groups: Arc::clone(&self.groups) }
    }

    /// Whether the pattern matches anywhere in `text`.
    pub fn is_match(&self, text: &str) -> bool {
        self.raw(text, None).next().is_some()
    }

    /// Like [`is_match`](Regex::is_match), searching from byte offset `start`.
    pub fn is_match_at(&self, text: &str, start: usize) -> bool {
        self.raw(text, Some(start)).next().is_some()
    }

    /// The leftmost match's whole-match span, or `None`.
    pub fn find<'t>(&self, text: &'t str) -> Option<Match<'t>> {
        self.raw(text, None).next().map(|s| Match::from_span(text, s[0]))
    }

    /// Like [`find`](Regex::find), searching from byte offset `start`.
    pub fn find_at<'t>(&self, text: &'t str, start: usize) -> Option<Match<'t>> {
        self.raw(text, Some(start)).next().map(|s| Match::from_span(text, s[0]))
    }

    /// Iterate the non-overlapping whole-match spans in `text`.
    pub fn find_iter<'r, 't>(&'r self, text: &'t str) -> Matches<'r, 't> {
        Matches { raw: self.raw(text, None), text }
    }

    /// The capture groups of the leftmost match, or `None`.
    pub fn captures<'t>(&self, text: &'t str) -> Option<Captures<'t>> {
        self.raw(text, None).next().map(|s| self.caps_from(text, s))
    }

    /// Like [`captures`](Regex::captures), searching from byte offset `start`.
    pub fn captures_at<'t>(&self, text: &'t str, start: usize) -> Option<Captures<'t>> {
        self.raw(text, Some(start)).next().map(|s| self.caps_from(text, s))
    }

    /// Iterate the capture groups of each non-overlapping match in `text`.
    pub fn captures_iter<'r, 't>(&'r self, text: &'t str) -> CaptureMatches<'r, 't> {
        CaptureMatches { raw: self.raw(text, None), re: self, text }
    }

    /// The end offset of the leftmost match (a match exists iff this is `Some`). **Divergence:** REAL is a
    /// leftmost-longest engine, so this returns the *longest* leftmost match's end, not the shortest as the
    /// regex crate does — use it as an `is_match` that also reports where the match ends.
    pub fn shortest_match(&self, text: &str) -> Option<usize> {
        self.raw(text, None).next().map(|s| s[0].unwrap().1)
    }
}

impl Drop for Regex {
    fn drop(&mut self) {
        unsafe { real_free(self.handle) }
    }
}

impl std::fmt::Debug for Regex {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Regex({:?})", self.pattern)
    }
}

// The low-level cursor: yields one match's full span vector at a time.
struct RawSpans<'r, 't> {
    iter: *mut RealIter,
    ngroups: usize,
    _re: PhantomData<(&'r Regex, &'t str)>,
}

impl RawSpans<'_, '_> {
    fn next(&mut self) -> Option<Vec<Option<(usize, usize)>>> {
        let mut spans = vec![0usize; 2 * self.ngroups];
        let got = unsafe { real_iter_next(self.iter, spans.as_mut_ptr()) };
        if got == 0 {
            return None;
        }
        Some(
            (0..self.ngroups)
                .map(|g| {
                    let (a, b) = (spans[2 * g], spans[2 * g + 1]);
                    if a == usize::MAX { None } else { Some((a, b)) }
                })
                .collect(),
        )
    }
}

impl Drop for RawSpans<'_, '_> {
    fn drop(&mut self) {
        unsafe { real_iter_free(self.iter) }
    }
}

/// A single match — one span into the subject (the whole match, or one capture group).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Match<'t> {
    text: &'t str,
    start: usize,
    end: usize,
}

impl<'t> Match<'t> {
    fn from_span(text: &'t str, span: Option<(usize, usize)>) -> Match<'t> {
        let (start, end) = span.expect("group 0 always participates in a match");
        Match { text, start, end }
    }

    /// The start byte offset.
    pub fn start(&self) -> usize {
        self.start
    }

    /// The end byte offset (exclusive).
    pub fn end(&self) -> usize {
        self.end
    }

    /// The byte range `start..end`.
    pub fn range(&self) -> std::ops::Range<usize> {
        self.start..self.end
    }

    /// The matched slice.
    pub fn as_str(&self) -> &'t str {
        &self.text[self.start..self.end]
    }

    /// Whether the match is empty.
    pub fn is_empty(&self) -> bool {
        self.start == self.end
    }

    /// The length of the match in bytes.
    pub fn len(&self) -> usize {
        self.end - self.start
    }
}

/// The capture groups of a single match. Group 0 is the whole match.
pub struct Captures<'t> {
    text: &'t str,
    spans: Vec<Option<(usize, usize)>>,
    groups: Arc<GroupInfo>,
}

impl<'t> Captures<'t> {
    /// Capture group `i` (0 = the whole match), or `None` if it did not participate.
    pub fn get(&self, i: usize) -> Option<Match<'t>> {
        self.spans.get(i).copied().flatten().map(|(s, e)| Match { text: self.text, start: s, end: e })
    }

    /// The named capture group `name`, or `None` if it is absent or did not participate.
    pub fn name(&self, name: &str) -> Option<Match<'t>> {
        self.groups.by_name.get(name).and_then(|&i| self.get(i))
    }

    /// The number of capture slots, including group 0.
    pub fn len(&self) -> usize {
        self.spans.len()
    }

    /// Whether there are no capture slots (never true for a real match — group 0 always exists).
    pub fn is_empty(&self) -> bool {
        self.spans.is_empty()
    }

    /// Iterate every group in order (`None` for a group that did not participate).
    pub fn iter(&self) -> impl Iterator<Item = Option<Match<'t>>> + '_ {
        (0..self.spans.len()).map(move |i| self.get(i))
    }
}

// Panicking index access, mirroring regex: caps[0] / caps["name"] return the matched &str.
impl Index<usize> for Captures<'_> {
    type Output = str;
    fn index(&self, i: usize) -> &str {
        self.get(i).map(|m| m.as_str()).unwrap_or_else(|| panic!("no group at index {i}"))
    }
}

impl Index<&str> for Captures<'_> {
    type Output = str;
    fn index(&self, name: &str) -> &str {
        self.name(name).map(|m| m.as_str()).unwrap_or_else(|| panic!("no group named {name:?}"))
    }
}

/// Iterator over whole-match spans, from [`Regex::find_iter`].
pub struct Matches<'r, 't> {
    raw: RawSpans<'r, 't>,
    text: &'t str,
}

impl<'t> Iterator for Matches<'_, 't> {
    type Item = Match<'t>;
    fn next(&mut self) -> Option<Match<'t>> {
        self.raw.next().map(|s| Match::from_span(self.text, s[0]))
    }
}

/// Iterator over capture groups, from [`Regex::captures_iter`].
pub struct CaptureMatches<'r, 't> {
    raw: RawSpans<'r, 't>,
    re: &'r Regex,
    text: &'t str,
}

impl<'t> Iterator for CaptureMatches<'_, 't> {
    type Item = Captures<'t>;
    fn next(&mut self) -> Option<Captures<'t>> {
        self.raw.next().map(|s| self.re.caps_from(self.text, s))
    }
}
