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

// Compile a pattern (as raw bytes) and precompute its group names. Shared by the str and bytes APIs.
fn compile_handle(pattern: &[u8], flags: u32) -> Result<(*mut RealRegex, usize, Arc<GroupInfo>), String> {
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
        let len = unsafe { real_group_name(handle, g, buf.as_mut_ptr() as *mut c_char, buf.len()) };
        if len == 0 {
            names.push(None);
        } else {
            let n = len.min(buf.len() - 1);
            let name = String::from_utf8_lossy(&buf[..n]).into_owned();
            by_name.insert(name.clone(), g);
            names.push(Some(name));
        }
    }
    Ok((handle, ngroups, Arc::new(GroupInfo { names, by_name })))
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
        let (handle, ngroups, groups) = compile_handle(pattern.as_bytes(), flags)?;
        Ok(Regex { handle, ngroups, pattern: pattern.to_string(), groups })
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
        RawSpans { iter, ngroups: self.ngroups, last_end: None, _re: PhantomData }
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

    /// The end offset of the leftmost match (a match exists iff this is `Some`). **Divergence:** like the
    /// regex crate, REAL is leftmost-**first**, but this returns the leftmost match's *greedy* end, whereas
    /// the regex crate returns the earliest position at which a match completes (e.g. `a+` on `"aaa"`: REAL
    /// 3, regex 1). A true earliest-completion mode is a parked follow-up (a `first-accept` stop in the
    /// forward pass). Use this as an `is_match` that also reports where the leftmost match ends.
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
    last_end: Option<usize>, // end of the last YIELDED match — for the rust empty-match iteration rule
    _re: PhantomData<(&'r (), &'t ())>, // lifetime-only marker — reused by the str and bytes APIs
}

impl RawSpans<'_, '_> {
    // One raw yield from the engine iterator (which follows re's empty-match rule).
    fn engine_next(&mut self) -> Option<Vec<Option<(usize, usize)>>> {
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

    // The rust empty-match rule, applied at the wrapper (regex-automata util::iter::Searcher::try_advance):
    // an empty match whose end equals the previous yielded match's end is skipped. REAL's engine yields the
    // re-superset (3.7+: it keeps such an empty), so filtering it here adapts the iteration to rust's
    // contract without touching the engine. split/replace inherit this through the same cursor.
    fn next(&mut self) -> Option<Vec<Option<(usize, usize)>>> {
        loop {
            let spans = self.engine_next()?;
            let (s0, e0) = spans[0].expect("group 0 always participates");
            if s0 == e0 && Some(e0) == self.last_end {
                continue; // empty match adjacent to the previous match end -> skip (rust's rule)
            }
            self.last_end = Some(e0);
            return Some(spans);
        }
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

// ── Flags (real::flags bits) ────────────────────────────────────────────────────────────────────────────
const FLAG_ICASE: u32 = 1;
const FLAG_MULTILINE: u32 = 2;
const FLAG_DOTALL: u32 = 4;
const FLAG_VERBOSE: u32 = 16;
const FLAG_ASCII: u32 = 64;

/// A builder for a [`Regex`] with readable options — the mirror of `regex::RegexBuilder`.
pub struct RegexBuilder {
    pattern: String,
    flags: u32,
}

impl RegexBuilder {
    /// Start building from `pattern`.
    pub fn new(pattern: &str) -> RegexBuilder {
        RegexBuilder { pattern: pattern.to_string(), flags: 0 }
    }

    fn set(&mut self, bit: u32, yes: bool) -> &mut RegexBuilder {
        if yes { self.flags |= bit } else { self.flags &= !bit }
        self
    }

    /// Case-insensitive matching (ASCII; REAL's icase). Maps to `(?i)`.
    pub fn case_insensitive(&mut self, yes: bool) -> &mut RegexBuilder {
        self.set(FLAG_ICASE, yes)
    }

    /// `^`/`$` match at line boundaries. Maps to `(?m)`.
    pub fn multi_line(&mut self, yes: bool) -> &mut RegexBuilder {
        self.set(FLAG_MULTILINE, yes)
    }

    /// `.` matches newlines. Maps to `(?s)`.
    pub fn dot_matches_new_line(&mut self, yes: bool) -> &mut RegexBuilder {
        self.set(FLAG_DOTALL, yes)
    }

    /// Verbose mode — insignificant whitespace and `#` comments. Maps to `(?x)`.
    pub fn ignore_whitespace(&mut self, yes: bool) -> &mut RegexBuilder {
        self.set(FLAG_VERBOSE, yes)
    }

    /// Unicode mode. `true` (the default) keeps REAL's Unicode str semantics; `false` restricts `\w \d \s \b`
    /// and case folding to ASCII (REAL's `ascii` flag), mirroring `regex`'s `unicode(false)`.
    pub fn unicode(&mut self, yes: bool) -> &mut RegexBuilder {
        self.set(FLAG_ASCII, !yes)
    }

    /// Accepted for API compatibility with `regex`; REAL enforces its own fixed complexity caps, so this is a
    /// no-op (there is no per-pattern memory budget to set).
    pub fn size_limit(&mut self, _bytes: usize) -> &mut RegexBuilder {
        self
    }

    /// Compile the configured pattern.
    pub fn build(&self) -> Result<Regex, String> {
        Regex::with_flags(&self.pattern, self.flags)
    }
}

// ── Replace ─────────────────────────────────────────────────────────────────────────────────────────────
use std::borrow::Cow;

/// A replacement value for [`Regex::replace`] and friends — a `&str`/`String` template (with `$0`, `$1`,
/// `$name`, `${name}` expansion and `$$` for a literal `$`), a [`NoExpand`] literal, or a closure
/// `FnMut(&Captures) -> impl AsRef<str>`.
pub trait Replacer {
    /// Append the replacement for `caps` to `dst`.
    fn replace_append(&mut self, caps: &Captures, dst: &mut String);
}

/// A literal replacement, with no `$` expansion (mirrors `regex::NoExpand`).
pub struct NoExpand<'a>(pub &'a str);

impl Replacer for NoExpand<'_> {
    fn replace_append(&mut self, _caps: &Captures, dst: &mut String) {
        dst.push_str(self.0);
    }
}

impl Replacer for &str {
    fn replace_append(&mut self, caps: &Captures, dst: &mut String) {
        expand(caps, self, dst);
    }
}

impl Replacer for String {
    fn replace_append(&mut self, caps: &Captures, dst: &mut String) {
        expand(caps, self, dst);
    }
}

impl<F, T> Replacer for F
where
    F: FnMut(&Captures) -> T,
    T: AsRef<str>,
{
    fn replace_append(&mut self, caps: &Captures, dst: &mut String) {
        dst.push_str((*self)(caps).as_ref());
    }
}

// Expand a `$`-template against caps. $$ -> $, $N / ${N} -> group N, $name / ${name} -> named group; an
// unknown group expands to nothing, as regex does. A `$` with no valid name following stays literal.
fn expand(caps: &Captures, template: &str, dst: &mut String) {
    let mut rest = template;
    while let Some(i) = rest.find('$') {
        dst.push_str(&rest[..i]);
        rest = &rest[i + 1..];
        if let Some(stripped) = rest.strip_prefix('$') {
            dst.push('$');
            rest = stripped;
            continue;
        }
        let (name, after) = if let Some(braced) = rest.strip_prefix('{') {
            match braced.find('}') {
                Some(j) => (&braced[..j], &braced[j + 1..]),
                None => {
                    dst.push('$');
                    ("", rest)
                }
            }
        } else {
            let end = rest.find(|c: char| !(c.is_ascii_alphanumeric() || c == '_')).unwrap_or(rest.len());
            (&rest[..end], &rest[end..])
        };
        rest = after;
        if name.is_empty() {
            dst.push('$');
            continue;
        }
        let m = match name.parse::<usize>() {
            Ok(n) => caps.get(n),
            Err(_) => caps.name(name),
        };
        if let Some(m) = m {
            dst.push_str(m.as_str());
        }
    }
    dst.push_str(rest);
}

impl Regex {
    /// Replace the leftmost match in `text` with `rep`. If there is no match, `text` is returned unchanged
    /// (borrowed).
    pub fn replace<'t, R: Replacer>(&self, text: &'t str, rep: R) -> Cow<'t, str> {
        self.replacen(text, 1, rep)
    }

    /// Replace every non-overlapping match in `text` with `rep`.
    pub fn replace_all<'t, R: Replacer>(&self, text: &'t str, rep: R) -> Cow<'t, str> {
        self.replacen(text, 0, rep)
    }

    /// Replace at most `limit` matches (`0` means all).
    pub fn replacen<'t, R: Replacer>(&self, text: &'t str, limit: usize, mut rep: R) -> Cow<'t, str> {
        let mut out: Option<String> = None;
        let mut last = 0;
        for (i, caps) in self.captures_iter(text).enumerate() {
            if limit != 0 && i >= limit {
                break;
            }
            let m = caps.get(0).unwrap();
            let dst = out.get_or_insert_with(|| String::with_capacity(text.len()));
            dst.push_str(&text[last..m.start()]);
            rep.replace_append(&caps, dst);
            last = m.end();
        }
        match out {
            Some(mut dst) => {
                dst.push_str(&text[last..]);
                Cow::Owned(dst)
            }
            None => Cow::Borrowed(text),
        }
    }

    /// Iterate the substrings of `text` delimited by matches (leading/trailing empties included), mirroring
    /// `regex::Regex::split`.
    pub fn split<'r, 't>(&'r self, text: &'t str) -> Split<'r, 't> {
        Split { text, it: self.find_iter(text), last: 0, done: false }
    }

    /// Like [`split`](Regex::split), but yielding at most `limit` substrings (the last is the unsplit
    /// remainder). `limit == 0` yields nothing.
    pub fn splitn<'r, 't>(&'r self, text: &'t str, limit: usize) -> SplitN<'r, 't> {
        SplitN { inner: self.split(text), limit, n: 0 }
    }
}

/// Iterator of the pieces between matches, from [`Regex::split`].
pub struct Split<'r, 't> {
    text: &'t str,
    it: Matches<'r, 't>,
    last: usize,
    done: bool,
}

impl<'t> Iterator for Split<'_, 't> {
    type Item = &'t str;
    fn next(&mut self) -> Option<&'t str> {
        if self.done {
            return None;
        }
        match self.it.next() {
            Some(m) => {
                let piece = &self.text[self.last..m.start()];
                self.last = m.end();
                Some(piece)
            }
            None => {
                self.done = true;
                Some(&self.text[self.last..])
            }
        }
    }
}

/// Iterator of at most `limit` pieces, from [`Regex::splitn`].
pub struct SplitN<'r, 't> {
    inner: Split<'r, 't>,
    limit: usize,
    n: usize,
}

impl<'t> Iterator for SplitN<'_, 't> {
    type Item = &'t str;
    fn next(&mut self) -> Option<&'t str> {
        if self.n >= self.limit {
            return None;
        }
        self.n += 1;
        if self.n == self.limit {
            // Last allowed piece: the unsplit remainder from the current cursor to the end.
            if self.inner.done {
                return None;
            }
            self.inner.done = true;
            return Some(&self.inner.text[self.inner.last..]);
        }
        self.inner.next()
    }
}

/// Byte-oriented regular expressions — the mirror of [`regex::bytes`], matching over `&[u8]` (which need not
/// be valid UTF-8). Patterns compile in REAL's raw-byte mode (`\w \d \s \b` are ASCII); every other method
/// mirrors the top-level string API. Group 0 is the whole match; spans are byte offsets.
pub mod bytes {
    use super::{compile_handle, real_find_iter, real_find_iter_at, real_free, GroupInfo,
                RawSpans, RealRegex, FLAG_ASCII, FLAG_DOTALL, FLAG_ICASE, FLAG_MULTILINE, FLAG_VERBOSE};
    use std::borrow::Cow;
    use std::marker::PhantomData;
    use std::ops::Index;
    use std::os::raw::c_char;
    use std::sync::Arc;

    const FLAG_BYTES: u32 = 8;

    /// A compiled byte pattern.
    pub struct Regex {
        handle: *mut RealRegex,
        ngroups: usize,
        pattern: Vec<u8>,
        groups: Arc<GroupInfo>,
    }

    unsafe impl Send for Regex {}
    unsafe impl Sync for Regex {}

    impl Regex {
        /// Compile `pattern` (given as text) in byte mode.
        pub fn new(pattern: &str) -> Result<Regex, String> {
            Regex::with_flags(pattern.as_bytes(), 0)
        }

        /// Compile a raw-byte pattern with extra `real::flags` (byte mode is always on).
        pub fn with_flags(pattern: &[u8], flags: u32) -> Result<Regex, String> {
            let (handle, ngroups, groups) = compile_handle(pattern, flags | FLAG_BYTES)?;
            Ok(Regex { handle, ngroups, pattern: pattern.to_vec(), groups })
        }

        /// The pattern bytes.
        pub fn as_bytes(&self) -> &[u8] {
            &self.pattern
        }

        /// The number of capture slots, including group 0.
        pub fn captures_len(&self) -> usize {
            self.ngroups
        }

        /// The name of each capture group (group 0 first), `None` for the unnamed ones.
        pub fn capture_names(&self) -> impl Iterator<Item = Option<&str>> {
            self.groups.names.iter().map(|o| o.as_deref())
        }

        fn raw<'r, 't>(&'r self, text: &'t [u8], start: Option<usize>) -> RawSpans<'r, 't> {
            let iter = unsafe {
                match start {
                    None => real_find_iter(self.handle, text.as_ptr() as *const c_char, text.len()),
                    Some(s) => real_find_iter_at(self.handle, text.as_ptr() as *const c_char, text.len(), s),
                }
            };
            RawSpans { iter, ngroups: self.ngroups, last_end: None, _re: PhantomData }
        }

        fn caps_from<'t>(&self, text: &'t [u8], spans: Vec<Option<(usize, usize)>>) -> Captures<'t> {
            Captures { text, spans, groups: Arc::clone(&self.groups) }
        }

        /// Whether the pattern matches anywhere in `text`.
        pub fn is_match(&self, text: &[u8]) -> bool {
            self.raw(text, None).next().is_some()
        }

        /// The leftmost whole match, or `None`.
        pub fn find<'t>(&self, text: &'t [u8]) -> Option<Match<'t>> {
            self.raw(text, None).next().map(|s| Match::at(text, s[0]))
        }

        /// Like [`find`](Regex::find), searching from byte offset `start`.
        pub fn find_at<'t>(&self, text: &'t [u8], start: usize) -> Option<Match<'t>> {
            self.raw(text, Some(start)).next().map(|s| Match::at(text, s[0]))
        }

        /// Iterate whole matches.
        pub fn find_iter<'r, 't>(&'r self, text: &'t [u8]) -> Matches<'r, 't> {
            Matches { raw: self.raw(text, None), text }
        }

        /// The capture groups of the leftmost match, or `None`.
        pub fn captures<'t>(&self, text: &'t [u8]) -> Option<Captures<'t>> {
            self.raw(text, None).next().map(|s| self.caps_from(text, s))
        }

        /// Iterate the capture groups of each match.
        pub fn captures_iter<'r, 't>(&'r self, text: &'t [u8]) -> CaptureMatches<'r, 't> {
            CaptureMatches { raw: self.raw(text, None), re: self, text }
        }

        /// Replace the leftmost match with `rep` (a `&[u8]`/`Vec<u8>` template with `$`-expansion, or a
        /// closure `FnMut(&Captures) -> impl AsRef<[u8]>`).
        pub fn replace<'t, R: Replacer>(&self, text: &'t [u8], rep: R) -> Cow<'t, [u8]> {
            self.replacen(text, 1, rep)
        }

        /// Replace every non-overlapping match with `rep`.
        pub fn replace_all<'t, R: Replacer>(&self, text: &'t [u8], rep: R) -> Cow<'t, [u8]> {
            self.replacen(text, 0, rep)
        }

        /// Replace at most `limit` matches (`0` = all).
        pub fn replacen<'t, R: Replacer>(&self, text: &'t [u8], limit: usize, mut rep: R) -> Cow<'t, [u8]> {
            let mut out: Option<Vec<u8>> = None;
            let mut last = 0;
            for (i, caps) in self.captures_iter(text).enumerate() {
                if limit != 0 && i >= limit {
                    break;
                }
                let m = caps.get(0).unwrap();
                let dst = out.get_or_insert_with(|| Vec::with_capacity(text.len()));
                dst.extend_from_slice(&text[last..m.start()]);
                rep.replace_append(&caps, dst);
                last = m.end();
            }
            match out {
                Some(mut dst) => {
                    dst.extend_from_slice(&text[last..]);
                    Cow::Owned(dst)
                }
                None => Cow::Borrowed(text),
            }
        }

        /// Iterate the pieces of `text` delimited by matches.
        pub fn split<'r, 't>(&'r self, text: &'t [u8]) -> Split<'r, 't> {
            Split { text, it: self.find_iter(text), last: 0, done: false }
        }
    }

    impl Drop for Regex {
        fn drop(&mut self) {
            unsafe { real_free(self.handle) }
        }
    }

    /// A single byte-span match.
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub struct Match<'t> {
        text: &'t [u8],
        start: usize,
        end: usize,
    }

    impl<'t> Match<'t> {
        fn at(text: &'t [u8], span: Option<(usize, usize)>) -> Match<'t> {
            let (start, end) = span.expect("group 0 always participates");
            Match { text, start, end }
        }
        /// The start byte offset.
        pub fn start(&self) -> usize { self.start }
        /// The end byte offset.
        pub fn end(&self) -> usize { self.end }
        /// The matched bytes.
        pub fn as_bytes(&self) -> &'t [u8] { &self.text[self.start..self.end] }
        /// The byte range.
        pub fn range(&self) -> std::ops::Range<usize> { self.start..self.end }
        /// Whether the match is empty.
        pub fn is_empty(&self) -> bool { self.start == self.end }
        /// The match length in bytes.
        pub fn len(&self) -> usize { self.end - self.start }
    }

    /// The capture groups of one byte match.
    pub struct Captures<'t> {
        text: &'t [u8],
        spans: Vec<Option<(usize, usize)>>,
        groups: Arc<GroupInfo>,
    }

    impl<'t> Captures<'t> {
        /// Capture group `i` (0 = whole match).
        pub fn get(&self, i: usize) -> Option<Match<'t>> {
            self.spans.get(i).copied().flatten().map(|(s, e)| Match { text: self.text, start: s, end: e })
        }
        /// The named capture group `name`.
        pub fn name(&self, name: &str) -> Option<Match<'t>> {
            self.groups.by_name.get(name).and_then(|&i| self.get(i))
        }
        /// The number of capture slots (incl. group 0).
        pub fn len(&self) -> usize { self.spans.len() }
        /// Whether there are no slots (never for a real match).
        pub fn is_empty(&self) -> bool { self.spans.is_empty() }
    }

    impl Index<usize> for Captures<'_> {
        type Output = [u8];
        fn index(&self, i: usize) -> &[u8] {
            self.get(i).map(|m| m.as_bytes()).unwrap_or_else(|| panic!("no group at index {i}"))
        }
    }

    impl Index<&str> for Captures<'_> {
        type Output = [u8];
        fn index(&self, name: &str) -> &[u8] {
            self.name(name).map(|m| m.as_bytes()).unwrap_or_else(|| panic!("no group named {name:?}"))
        }
    }

    /// Iterator over whole matches.
    pub struct Matches<'r, 't> {
        raw: RawSpans<'r, 't>,
        text: &'t [u8],
    }

    impl<'t> Iterator for Matches<'_, 't> {
        type Item = Match<'t>;
        fn next(&mut self) -> Option<Match<'t>> {
            self.raw.next().map(|s| Match::at(self.text, s[0]))
        }
    }

    /// Iterator over capture groups.
    pub struct CaptureMatches<'r, 't> {
        raw: RawSpans<'r, 't>,
        re: &'r Regex,
        text: &'t [u8],
    }

    impl<'t> Iterator for CaptureMatches<'_, 't> {
        type Item = Captures<'t>;
        fn next(&mut self) -> Option<Captures<'t>> {
            self.raw.next().map(|s| self.re.caps_from(self.text, s))
        }
    }

    /// Iterator of the pieces between matches.
    pub struct Split<'r, 't> {
        text: &'t [u8],
        it: Matches<'r, 't>,
        last: usize,
        done: bool,
    }

    impl<'t> Iterator for Split<'_, 't> {
        type Item = &'t [u8];
        fn next(&mut self) -> Option<&'t [u8]> {
            if self.done {
                return None;
            }
            match self.it.next() {
                Some(m) => {
                    let piece = &self.text[self.last..m.start()];
                    self.last = m.end();
                    Some(piece)
                }
                None => {
                    self.done = true;
                    Some(&self.text[self.last..])
                }
            }
        }
    }

    /// A byte replacement — a `&[u8]`/`Vec<u8>` template (with `$0`/`$1`/`$name`/`${name}`/`$$`), a
    /// [`NoExpand`] literal, or a closure `FnMut(&Captures) -> impl AsRef<[u8]>`.
    pub trait Replacer {
        /// Append the replacement for `caps` to `dst`.
        fn replace_append(&mut self, caps: &Captures, dst: &mut Vec<u8>);
    }

    /// A literal byte replacement, no `$` expansion.
    pub struct NoExpand<'a>(pub &'a [u8]);

    impl Replacer for NoExpand<'_> {
        fn replace_append(&mut self, _caps: &Captures, dst: &mut Vec<u8>) {
            dst.extend_from_slice(self.0);
        }
    }

    impl Replacer for &[u8] {
        fn replace_append(&mut self, caps: &Captures, dst: &mut Vec<u8>) {
            expand_bytes(caps, self, dst);
        }
    }

    impl<F, T> Replacer for F
    where
        F: FnMut(&Captures) -> T,
        T: AsRef<[u8]>,
    {
        fn replace_append(&mut self, caps: &Captures, dst: &mut Vec<u8>) {
            dst.extend_from_slice((*self)(caps).as_ref());
        }
    }

    // Byte-template expansion: $$ -> $, $N / ${N} -> group N, $name / ${name} -> named group; an unknown
    // group expands to nothing, a lone `$` stays literal — the same rules as the str expander.
    fn expand_bytes(caps: &Captures, template: &[u8], dst: &mut Vec<u8>) {
        let mut i = 0;
        while i < template.len() {
            let b = template[i];
            if b != b'$' {
                dst.push(b);
                i += 1;
                continue;
            }
            i += 1; // consume '$'
            if i < template.len() && template[i] == b'$' {
                dst.push(b'$');
                i += 1;
                continue;
            }
            let (name, next) = if i < template.len() && template[i] == b'{' {
                match template[i + 1..].iter().position(|&c| c == b'}') {
                    Some(j) => (&template[i + 1..i + 1 + j], i + 1 + j + 1),
                    None => {
                        dst.push(b'$');
                        continue;
                    }
                }
            } else {
                let mut j = i;
                while j < template.len() && (template[j].is_ascii_alphanumeric() || template[j] == b'_') {
                    j += 1;
                }
                (&template[i..j], j)
            };
            i = next;
            if name.is_empty() {
                dst.push(b'$');
                continue;
            }
            let name_str = std::str::from_utf8(name).unwrap_or("");
            let m = match name_str.parse::<usize>() {
                Ok(n) => caps.get(n),
                Err(_) => caps.name(name_str),
            };
            if let Some(m) = m {
                dst.extend_from_slice(m.as_bytes());
            }
        }
    }

    /// A builder for a byte [`Regex`] — the mirror of `regex::bytes::RegexBuilder`.
    pub struct RegexBuilder {
        pattern: Vec<u8>,
        flags: u32,
    }

    impl RegexBuilder {
        /// Start building from `pattern`.
        pub fn new(pattern: &str) -> RegexBuilder {
            RegexBuilder { pattern: pattern.as_bytes().to_vec(), flags: 0 }
        }
        fn set(&mut self, bit: u32, yes: bool) -> &mut RegexBuilder {
            if yes { self.flags |= bit } else { self.flags &= !bit }
            self
        }
        /// Case-insensitive matching (ASCII).
        pub fn case_insensitive(&mut self, yes: bool) -> &mut RegexBuilder { self.set(FLAG_ICASE, yes) }
        /// `^`/`$` match at line boundaries.
        pub fn multi_line(&mut self, yes: bool) -> &mut RegexBuilder { self.set(FLAG_MULTILINE, yes) }
        /// `.` matches newlines.
        pub fn dot_matches_new_line(&mut self, yes: bool) -> &mut RegexBuilder { self.set(FLAG_DOTALL, yes) }
        /// Verbose mode.
        pub fn ignore_whitespace(&mut self, yes: bool) -> &mut RegexBuilder { self.set(FLAG_VERBOSE, yes) }
        /// Unicode mode (`false` restricts `\w \d \s` to ASCII).
        pub fn unicode(&mut self, yes: bool) -> &mut RegexBuilder { self.set(FLAG_ASCII, !yes) }
        /// Accepted for API compatibility; a no-op (REAL has fixed complexity caps).
        pub fn size_limit(&mut self, _bytes: usize) -> &mut RegexBuilder { self }
        /// Compile.
        pub fn build(&self) -> Result<Regex, String> {
            Regex::with_flags(&self.pattern, self.flags)
        }
    }
}
