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
//! let re: Regex = r"\d+".parse().unwrap();
//! assert_eq!(format!("{re}"), r"\d+");
//! ```
use std::collections::HashMap;
use std::marker::PhantomData;
use std::ops::Index;
use std::os::raw::c_char;
use std::sync::Arc;

/// The crate's version (CalVer, shared with the C++ engine and the Python wheel).
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

// Opaque C handles.
enum RealRegex {}
enum RealIter {}
enum RealRegexSet {}

extern "C" {
    fn real_compile(pattern: *const c_char, len: usize, flags: u32,
                    errbuf: *mut c_char, errbuf_len: usize, code: *mut i32) -> *mut RealRegex;
    fn real_group_count(re: *const RealRegex) -> usize;
    fn real_group_name(re: *const RealRegex, group: usize, buf: *mut c_char, buflen: usize) -> usize;
    fn real_free(re: *mut RealRegex);
    fn real_find_iter(re: *const RealRegex, text: *const c_char, len: usize) -> *mut RealIter;
    fn real_find_iter_at(re: *const RealRegex, text: *const c_char, len: usize, start: usize) -> *mut RealIter;
    fn real_iter_next(iter: *mut RealIter, spans: *mut usize) -> i32;
    fn real_iter_free(iter: *mut RealIter);
    fn real_count_matches(re: *const RealRegex, text: *const c_char, len: usize) -> usize;
    fn real_set_compile(patterns: *const *const c_char, lens: *const usize, n: usize, flags: u32,
                        errbuf: *mut c_char, errbuf_len: usize, code: *mut i32) -> *mut RealRegexSet;
    fn real_set_size(set: *const RealRegexSet) -> usize;
    fn real_set_free(set: *mut RealRegexSet);
    fn real_set_is_match(set: *const RealRegexSet, text: *const c_char, len: usize) -> i32;
    fn real_set_matches(set: *const RealRegexSet, text: *const c_char, len: usize, out: *mut u8) -> i32;
}

const DIVERGENCES_URL: &str = "https://github.com/RECHE23/real-regex/blob/main/docs/COMPATIBILITY.md";
const REAL_ERR_UNSUPPORTED: i32 = 2; // must match REAL_ERR_UNSUPPORTED in real_capi.h
// real::flags::dollar_endonly — `$` (no multiline) matches only at the very end, never before a final `\n`.
// The crate compiles every pattern with it, so `$` carries rust's `\z` semantics instead of Python re's.
const DOLLAR_ENDONLY: u32 = 128;

/// Why a pattern failed to compile.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    /// A syntax error in the pattern, with the engine's message and (when known) the byte position.
    Syntax { msg: String, pos: Option<usize> },
    /// A construct REAL does not support linearly (`\p{…}`, a backreference, an unbounded lookaround, …).
    /// `hint` points at the divergences page and, when there is one, the way out — the `fallback`
    /// feature. It names the absence just as plainly: a [`RegexSet`] never delegates, and the regex
    /// crate is linear too, so it refuses a backreference exactly as REAL does. The hint sells a
    /// remedy only where one exists.
    Unsupported { construct: String, hint: String },
}

impl Error {
    /// Whether this is an unsupported-construct error (rather than a syntax error).
    pub fn is_unsupported(&self) -> bool {
        matches!(self, Error::Unsupported { .. })
    }

    // Build an Error from the engine's message and its structured code (REAL_ERR_*). The classification comes
    // from the code the C ABI reports — never from matching on the message text, so a reworded engine message
    // cannot silently change whether a pattern is treated as unsupported.
    fn from_engine(raw: &str, code: i32, rescue: Rescue) -> Error {
        let body = raw.strip_prefix("regex_error").unwrap_or(raw).trim_start();
        let (pos, msg) = match body.strip_prefix("at ").and_then(|r| r.split_once(':')) {
            Some((n, rest)) => (n.trim().parse::<usize>().ok(), rest.trim().to_string()),
            None => (None, body.trim_start_matches(':').trim().to_string()),
        };
        if code == REAL_ERR_UNSUPPORTED {
            unsupported_construct(&msg, rescue)
        } else {
            Error::Syntax { msg, pos }
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::Syntax { msg, pos: Some(p) } => write!(f, "syntax error at {p}: {msg}"),
            Error::Syntax { msg, pos: None } => write!(f, "syntax error: {msg}"),
            Error::Unsupported { construct, hint } => write!(f, "{construct} ({hint})"),
        }
    }
}

impl std::error::Error for Error {}

// Group metadata, shared cheaply (Arc) by the Regex and every Captures it produces — this is what lets
// Captures carry a single lifetime, like the regex crate.
struct GroupInfo {
    names: Vec<Option<String>>,       // by group index (None = unnamed)
    by_name: HashMap<String, usize>,  // name -> group index
}

// Inline capacity of a Captures, in SLOTS (two per group, group 0 included) — 8 slots = 4 groups.
// Covers the overwhelming majority of real patterns; beyond it a Captures spills to the heap once.
const CAPS_INLINE_SLOTS: usize = 8;

// Capture slots for one match, flat and inline: [start0, end0, start1, end1, …], usize::MAX marking a
// group that did not participate — the same representation the C ABI fills and CaptureLocations holds,
// so building one is a straight copy with no per-group Option mapping.
//
// Why inline: Captures must OWN its slots (it outlives the iterator step that produced it), and the
// previous Vec<Option<(usize, usize)>> meant one malloc + free per match. On a groupless pattern that
// was ~19–27 ns/match of pure allocator traffic to carry a single span — measured as the whole of the
// crate's captures_iter-vs-find_iter gap, and the reason `regex`'s captures_iter costs what its
// find_iter costs while ours cost 1.6–2.6× more. Spilling keeps the many-group case correct rather
// than capping it.
#[derive(Clone, Debug)]
enum SlotStore {
    Inline { len: u8, slots: [usize; CAPS_INLINE_SLOTS] },
    Spilled(Box<[usize]>),
}

impl SlotStore {
    // Take a flat slot run (len = 2 * ngroups) by value-copy, inline when it fits.
    fn from_flat(src: &[usize]) -> SlotStore {
        // Group 0 alone -- a groupless pattern -- is the dominant shape, and taking it with two plain
        // stores rather than `copy_from_slice` is the whole point: a runtime length compiles to a memcpy
        // CALL, which costs more than the stores it replaces at one or two slots. That is the same reason
        // the C ABI reads slots pairwise instead of with one memcpy, and it was measured here too:
        // ablating this call closed the entire remaining captures_iter-vs-find_iter gap (114 of the 171 us
        // on `\b\w+\b` over a 64 KiB corpus, ~9.4 ns a match), where the object's size, Drop glue and Arc
        // traffic together accounted for the other 57.
        if src.len() == 2 {
            let mut slots = [usize::MAX; CAPS_INLINE_SLOTS];
            slots[0] = src[0];
            slots[1] = src[1];
            return SlotStore::Inline { len: 2, slots };
        }
        if src.len() <= CAPS_INLINE_SLOTS {
            let mut slots = [usize::MAX; CAPS_INLINE_SLOTS];
            slots[..src.len()].copy_from_slice(src);
            SlotStore::Inline { len: src.len() as u8, slots }
        } else {
            SlotStore::Spilled(src.to_vec().into_boxed_slice())
        }
    }

    fn as_slice(&self) -> &[usize] {
        match self {
            SlotStore::Inline { len, slots } => &slots[..*len as usize],
            SlotStore::Spilled(b) => b,
        }
    }

    // Byte offsets of group `i`, or None when it did not participate (or `i` is out of range).
    // checked_mul, not `2 * i`: `i` is caller-supplied (Captures::get / Index take any usize), and a
    // plain multiply panics on overflow in a debug build for i > usize::MAX / 2. The slot indexing is
    // this type's own doing -- the Vec<Option<_>> this replaced was indexed by group, so it could not
    // overflow -- so the bound has to be re-established here. `lo + 1` cannot overflow: lo came back
    // from a successful get() on a slice, so lo < len <= isize::MAX.
    fn group(&self, i: usize) -> Option<(usize, usize)> {
        let s = self.as_slice();
        let lo = i.checked_mul(2)?;
        let a = *s.get(lo)?;
        let b = *s.get(lo + 1)?;
        if a == usize::MAX {
            None
        } else {
            Some((a, b))
        }
    }

    // Slot count / 2 — the number of groups, group 0 included.
    fn ngroups(&self) -> usize {
        self.as_slice().len() / 2
    }
}

/// Whether the `fallback` feature could actually rescue the pattern that was just refused.
///
/// Two halves must hold before the hint is worth printing, the same pair the Python binding
/// settled on: the call site has a fallback at all, and the delegate can run the pattern. The
/// second half is where Rust differs from Python — `re` backtracks and takes anything, the regex
/// crate is linear and refuses a backreference exactly as REAL does (pinned in tests/fallback.rs).
// Without the feature the regex crate is not linked, so `rescue_for` can only ever answer
// `Unknown` and the two decided variants are unconstructible — a fact of that build, not dead code.
#[cfg_attr(not(feature = "fallback"), allow(dead_code))]
enum Rescue {
    /// The delegate accepts it — say so, and name the switch.
    Delegable,
    /// The delegate refuses it too. Offering the feature here sends the reader down a dead end.
    Refused,
    /// No oracle: the feature is off, so the regex crate is not linked and cannot be asked.
    Unknown,
    /// This call site has no fallback whatever the construct — `RegexSet` never delegates.
    NoFallbackHere,
}

/// Ask the delegate itself rather than classifying the construct by hand. A hand-written list of
/// "constructs the regex crate refuses" would be a second model to keep true; the crate is the
/// authority on its own grammar.
#[cfg(feature = "fallback")]
fn rescue_for(pattern: &[u8]) -> Rescue {
    match std::str::from_utf8(pattern) {
        Ok(p) if regex::Regex::new(p).is_ok() => Rescue::Delegable,
        Ok(_) => Rescue::Refused,
        Err(_) => Rescue::Unknown,
    }
}

#[cfg(not(feature = "fallback"))]
fn rescue_for(_pattern: &[u8]) -> Rescue {
    Rescue::Unknown
}

// The standard unsupported-construct error, hint included (shared by the engine path and the pre-scan below).
fn unsupported_construct(construct: &str, rescue: Rescue) -> Error {
    let remedy = match rescue {
        Rescue::Delegable => "the `fallback` feature plus `RegexBuilder::fallback(true)` delegates this \
                              pattern to the regex crate (forfeiting the linear-time guarantee for it)"
            .to_string(),
        Rescue::Refused => "the `fallback` feature does not help here: the regex crate is linear too and \
                            refuses this pattern as well"
            .to_string(),
        Rescue::Unknown => "the `fallback` feature delegates some such patterns to the regex crate, but not \
                            a non-regular one (a backreference, a conditional) — the regex crate, linear \
                            itself, refuses those too"
            .to_string(),
        Rescue::NoFallbackHere => "a RegexSet never delegates: compile the pattern on its own with the \
                                   `fallback` feature if you need it"
            .to_string(),
    };
    Error::Unsupported {
        construct: construct.to_string(),
        hint: format!("unsupported by REAL — see {DIVERGENCES_URL} ; {remedy}"),
    }
}

// Rust's regex crate parses nested character classes (`[a[b]]` = union) and the class set operators `&&`,
// `--`, `~~`; Python `re` — REAL's model — treats `[` as a literal inside a class, so `[a[b]]` parses to two
// different classes (and two match sets). Rather than implement rust's class algebra, the crate declines such
// patterns up front with a hint (the `fallback` feature then delegates them, and `regex` does support them).
// Returns the offending construct, or None. Escapes (`\[`, `\-`, `\\`) are respected. `\p{…}` is a separate
// arc; here we only spot the class-set syntax.
fn nested_class_syntax(pattern: &[u8]) -> Option<&'static str> {
    let mut i = 0;
    let mut in_class = false;
    let mut class_pos = 0usize; // members seen in the current class (0 = just after `[` / `[^`)
    while i < pattern.len() {
        let b = pattern[i];
        if b == b'\\' {
            i += 2; // skip the escaped byte — an escaped `[` is a literal, never a nested class
            if in_class {
                class_pos += 1;
            }
            continue;
        }
        if !in_class {
            if b == b'[' {
                in_class = true;
                class_pos = 0;
                if pattern.get(i + 1) == Some(&b'^') {
                    i += 1; // negation; the first real member is still class_pos 0
                }
            }
        } else if b == b']' {
            if class_pos == 0 {
                class_pos += 1; // a `]` right after `[` is a literal member, not the close
            } else {
                in_class = false;
            }
        } else if b == b'[' {
            return Some("nested character class");
        } else if matches!(b, b'&' | b'-' | b'~') && pattern.get(i + 1) == Some(&b) {
            return Some("character-class set operation");
        } else {
            class_pos += 1;
        }
        i += 1;
    }
    None
}

// Compile a pattern (as raw bytes) and precompute its group names. Shared by the str and bytes APIs.
fn compile_handle(pattern: &[u8], flags: u32) -> Result<(*mut RealRegex, usize, Arc<GroupInfo>), Error> {
    if let Some(construct) = nested_class_syntax(pattern) {
        return Err(unsupported_construct(construct, rescue_for(pattern))); // rust-only class syntax REAL would parse differently
    }
    let mut err = [0u8; 256];
    let mut code: i32 = 0;
    let handle = unsafe {
        real_compile(pattern.as_ptr() as *const c_char, pattern.len(), flags | DOLLAR_ENDONLY,
                     err.as_mut_ptr() as *mut c_char, err.len(), &mut code)
    };
    if handle.is_null() {
        let end = err.iter().position(|&b| b == 0).unwrap_or(err.len());
        return Err(Error::from_engine(&String::from_utf8_lossy(&err[..end]), code, rescue_for(pattern)));
    }
    let ngroups = unsafe { real_group_count(handle) };
    let mut names = Vec::with_capacity(ngroups);
    let mut by_name = HashMap::new();
    // Two-call protocol (same shape as Go SubexpNames): length query with null buf, then
    // exact-sized fill — no fixed buffer, no 127-byte truncation / name-map alias collapse.
    for g in 0..ngroups {
        let len = unsafe { real_group_name(handle, g, std::ptr::null_mut(), 0) };
        if len == 0 {
            names.push(None);
        } else {
            let mut buf = vec![0u8; len + 1];
            unsafe {
                real_group_name(handle, g, buf.as_mut_ptr() as *mut c_char, buf.len());
            }
            let name = String::from_utf8_lossy(&buf[..len]).into_owned();
            by_name.insert(name.clone(), g);
            names.push(Some(name));
        }
    }
    Ok((handle, ngroups, Arc::new(GroupInfo { names, by_name })))
}

/// Which engine backs a compiled pattern — observable via [`Regex::engine`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Engine {
    /// REAL's linear-time, ReDoS-safe engine.
    Real,
    /// The regex crate (only when the `fallback` feature delegated this pattern) — not ReDoS-safe.
    Fallback,
}

/// A compiled pattern.
pub struct Regex {
    handle: *mut RealRegex, // null when a fallback backend is in use
    ngroups: usize,         // capture slots per match, including group 0
    pattern: String,
    groups: Arc<GroupInfo>,
    #[cfg(feature = "fallback")]
    fallback: Option<regex::Regex>, // Some when delegated to the regex crate
}

// The handle is an owned heap object with no interior mutability observable from Rust; sharing a &Regex
// across threads (read-only matching) is sound.
unsafe impl Send for Regex {}
unsafe impl Sync for Regex {}

impl Regex {
    /// Compile `pattern`. Returns the engine's error message if the pattern is invalid or cannot be run
    /// linearly (the strict policy).
    pub fn new(pattern: &str) -> Result<Regex, Error> {
        Regex::with_flags(pattern, 0)
    }

    /// Compile with a `real::flags` bitmask (icase=1, multiline=2, dotall=4, bytes=8, verbose=16, ecma=32,
    /// ascii=64). Prefer [`RegexBuilder`] for readable options.
    pub fn with_flags(pattern: &str, flags: u32) -> Result<Regex, Error> {
        let (handle, ngroups, groups) = compile_handle(pattern.as_bytes(), flags)?;
        Ok(Regex {
            handle,
            ngroups,
            pattern: pattern.to_string(),
            groups,
            #[cfg(feature = "fallback")]
            fallback: None,
        })
    }

    /// Which engine backs this pattern — [`Engine::Real`] (linear, ReDoS-safe) or [`Engine::Fallback`] (the
    /// regex crate, when the `fallback` feature delegated it). Always `Real` unless the feature is used.
    pub fn engine(&self) -> Engine {
        #[cfg(feature = "fallback")]
        if self.fallback.is_some() {
            return Engine::Fallback;
        }
        Engine::Real
    }

    // Delegate a pattern REAL cannot run linearly to the regex crate (only reachable via the `fallback`
    // feature + RegexBuilder::fallback(true)). The wrapper keeps our own types over regex's results.
    #[cfg(feature = "fallback")]
    fn build_fallback(pattern: &str, flags: u32) -> Result<Regex, Error> {
        let fb = regex::RegexBuilder::new(pattern)
            .case_insensitive(flags & FLAG_ICASE != 0)
            .multi_line(flags & FLAG_MULTILINE != 0)
            .dot_matches_new_line(flags & FLAG_DOTALL != 0)
            .ignore_whitespace(flags & FLAG_VERBOSE != 0)
            .unicode(flags & FLAG_ASCII == 0)
            .build()
            .map_err(|e| Error::Syntax { msg: e.to_string(), pos: None })?;
        let ngroups = fb.captures_len();
        let mut names = Vec::with_capacity(ngroups);
        let mut by_name = HashMap::new();
        for (i, n) in fb.capture_names().enumerate() {
            match n {
                Some(name) => {
                    by_name.insert(name.to_string(), i);
                    names.push(Some(name.to_string()));
                }
                None => names.push(None),
            }
        }
        Ok(Regex {
            handle: std::ptr::null_mut(),
            ngroups,
            pattern: pattern.to_string(),
            groups: Arc::new(GroupInfo { names, by_name }),
            fallback: Some(fb),
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

    fn raw<'r, 't>(&'r self, text: &'t str, start: Option<usize>) -> SpanCursor<'r, 't> {
        #[cfg(feature = "fallback")]
        if let Some(fb) = &self.fallback {
            return SpanCursor::Fallback {
                it: fb.captures_iter(text),
                ngroups: self.ngroups,
                min_start: start.unwrap_or(0),
                cur: Vec::new(),
            };
        }
        let iter = unsafe {
            match start {
                None => real_find_iter(self.handle, text.as_ptr() as *const c_char, text.len()),
                Some(s) => real_find_iter_at(self.handle, text.as_ptr() as *const c_char, text.len(), s),
            }
        };
        // A null cursor means the engine failed to construct the iterator (never dereference it).
        assert!(!iter.is_null(), "real-regex: engine iteration failed");
        SpanCursor::Real(RawSpans { iter, handle: self.handle, text: text.as_bytes(), ngroups: self.ngroups, buf: vec![0usize; 2 * self.ngroups], origin: start.unwrap_or(0), last_end: None, drive_pos: None, utf8: true, _re: PhantomData })
    }

    fn caps_from<'t>(&self, text: &'t str, cur: &SpanCursor<'_, '_>) -> Captures<'t> {
        Captures { text, slots: cur.slot_store(), groups: Arc::clone(&self.groups) }
    }

    /// Whether the pattern matches anywhere in `text`.
    pub fn is_match(&self, text: &str) -> bool {
        self.raw(text, None).advance().is_some()
    }

    /// Like [`is_match`](Regex::is_match), searching from byte offset `start`.
    pub fn is_match_at(&self, text: &str, start: usize) -> bool {
        self.raw(text, Some(start)).advance().is_some()
    }

    /// The leftmost match's whole-match span, or `None`.
    pub fn find<'t>(&self, text: &'t str) -> Option<Match<'t>> {
        self.raw(text, None).advance().map(|(a, b)| Match { text, start: a, end: b })
    }

    /// Like [`find`](Regex::find), searching from byte offset `start`.
    pub fn find_at<'t>(&self, text: &'t str, start: usize) -> Option<Match<'t>> {
        self.raw(text, Some(start)).advance().map(|(a, b)| Match { text, start: a, end: b })
    }

    /// Iterate the non-overlapping whole-match spans in `text`.
    pub fn find_iter<'r, 't>(&'r self, text: &'t str) -> Matches<'r, 't> {
        Matches { raw: self.raw(text, None), text }
    }

    /// The capture groups of the leftmost match, or `None`.
    pub fn captures<'t>(&self, text: &'t str) -> Option<Captures<'t>> {
        {
            let mut c = self.raw(text, None);
            c.advance().map(|_| self.caps_from(text, &c))
        }
    }

    /// Like [`captures`](Regex::captures), searching from byte offset `start`.
    pub fn captures_at<'t>(&self, text: &'t str, start: usize) -> Option<Captures<'t>> {
        {
            let mut c = self.raw(text, Some(start));
            c.advance().map(|_| self.caps_from(text, &c))
        }
    }

    /// A reusable capture-slot buffer for this pattern — drop-in for
    /// [`regex::Regex::capture_locations`]. Pair with [`captures_read`](Regex::captures_read)
    /// to extract groups without allocating a [`Captures`] per match.
    pub fn capture_locations(&self) -> CaptureLocations {
        CaptureLocations {
            slots: vec![0; 2 * self.ngroups],
            ngroups: self.ngroups,
        }
    }

    /// Fill `locs` with the leftmost match's group spans (no per-match allocation). Returns the
    /// whole-match [`Match`] span, or `None`. Mirrors `regex::Regex::captures_read`.
    pub fn captures_read<'t>(
        &self,
        locs: &mut CaptureLocations,
        text: &'t str,
    ) -> Option<Match<'t>> {
        self.captures_read_at(locs, text, 0)
    }

    /// Like [`captures_read`](Regex::captures_read), searching from byte offset `start`.
    pub fn captures_read_at<'t>(
        &self,
        locs: &mut CaptureLocations,
        text: &'t str,
        start: usize,
    ) -> Option<Match<'t>> {
        locs.ensure(self.ngroups);
        let mut c = self.raw(text, if start == 0 { None } else { Some(start) });
        let (a, b) = c.advance()?;
        c.copy_slots_into(locs);
        Some(Match {
            text,
            start: a,
            end: b,
        })
    }

    /// Iterate non-overlapping matches without allocating a [`Captures`] per match.
    /// Yields the whole-match [`Match`]; after each step, read groups with
    /// [`CaptureLocationMatches::get`] (or copy into a [`CaptureLocations`] via
    /// [`CaptureLocationMatches::read_captures`]). Prefer this over
    /// [`captures_iter`](Regex::captures_iter) in capture-dense hot loops.
    pub fn captures_read_iter<'r, 't>(
        &'r self,
        text: &'t str,
    ) -> CaptureLocationMatches<'r, 't> {
        CaptureLocationMatches {
            raw: self.raw(text, None),
            text,
            ngroups: self.ngroups,
        }
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
        #[cfg(feature = "fallback")]
        if let Some(fb) = &self.fallback {
            return fb.shortest_match(text); // the regex backend gives true earliest-completion
        }
        self.raw(text, None).advance().map(|(_, e)| e)
    }

    /// Count non-overlapping matches without materialising match objects (matching-only).
    ///
    /// Prefer this over counting [`find_iter`](Regex::find_iter) when only the count matters, and for
    /// trailing-lookahead class+ patterns where the fast path lives here (not on find_iter). Parity:
    /// `re.count_matches(t) == re.find_iter(t).count()`.
    pub fn count_matches(&self, text: &str) -> usize {
        #[cfg(feature = "fallback")]
        if let Some(fb) = &self.fallback {
            return fb.find_iter(text).count();
        }
        let n = unsafe {
            real_count_matches(self.handle, text.as_ptr() as *const c_char, text.len())
        };
        assert_ne!(n, usize::MAX, "real-regex: count_matches failed");
        n
    }
}

/// A multi-pattern set: which patterns match the subject at least once (which-matched).
///
/// Mirrors the [`regex`](https://docs.rs/regex) crate's `RegexSet`. Bitset order is the
/// construction order of the patterns. Captures are not reported — re-run the individual
/// pattern if groups are needed. Stage-1 is N independent walks with per-pattern early-exit
/// (not a fused single-pass automaton).
pub struct RegexSet {
    handle: *mut RealRegexSet,
    patterns: Vec<String>,
}

unsafe impl Send for RegexSet {}
unsafe impl Sync for RegexSet {}

impl RegexSet {
    /// Compile every pattern; fails if any pattern is invalid (no silent skip).
    pub fn new<I, S>(patterns: I) -> Result<RegexSet, Error>
    where
        I: IntoIterator<Item = S>,
        S: AsRef<str>,
    {
        RegexSet::with_flags(patterns, 0)
    }

    /// Compile with a `real::flags` bitmask (same bits as [`Regex::with_flags`]).
    pub fn with_flags<I, S>(patterns: I, flags: u32) -> Result<RegexSet, Error>
    where
        I: IntoIterator<Item = S>,
        S: AsRef<str>,
    {
        let owned: Vec<String> = patterns.into_iter().map(|s| s.as_ref().to_string()).collect();
        let mut ptrs: Vec<*const c_char> = Vec::with_capacity(owned.len());
        let mut lens: Vec<usize> = Vec::with_capacity(owned.len());
        for p in &owned {
            ptrs.push(p.as_ptr() as *const c_char);
            lens.push(p.len());
        }
        let mut err = [0i8; 512];
        let mut code: i32 = 0;
        let handle = unsafe {
            real_set_compile(
                ptrs.as_ptr(),
                lens.as_ptr(),
                owned.len(),
                flags | DOLLAR_ENDONLY,
                err.as_mut_ptr(),
                err.len(),
                &mut code,
            )
        };
        if handle.is_null() {
            let raw = unsafe { std::ffi::CStr::from_ptr(err.as_ptr()) }
                .to_string_lossy()
                .into_owned();
            return Err(Error::from_engine(&raw, code, Rescue::NoFallbackHere));
        }
        Ok(RegexSet {
            handle,
            patterns: owned,
        })
    }

    /// Number of patterns in the set.
    pub fn len(&self) -> usize {
        unsafe { real_set_size(self.handle) }
    }

    /// Whether the set has no patterns.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// The original pattern strings (construction order).
    pub fn patterns(&self) -> &[String] {
        &self.patterns
    }

    /// True if **any** pattern matches `text` (stops at the first hit).
    pub fn is_match(&self, text: &str) -> bool {
        let r = unsafe {
            real_set_is_match(self.handle, text.as_ptr() as *const c_char, text.len())
        };
        r == 1
    }

    /// Which patterns match at least once: bitset of length [`len`](RegexSet::len),
    /// construction order. Index `i` is true iff pattern `i` matched.
    pub fn matches(&self, text: &str) -> Vec<bool> {
        let n = self.len();
        let mut out = vec![0u8; n];
        let r = unsafe {
            real_set_matches(
                self.handle,
                text.as_ptr() as *const c_char,
                text.len(),
                out.as_mut_ptr(),
            )
        };
        assert_eq!(r, 0, "real-regex: regex_set matches failed");
        out.into_iter().map(|b| b != 0).collect()
    }

    /// Indices of matching patterns (ascending, construction order).
    pub fn matched_ids(&self, text: &str) -> Vec<usize> {
        self.matches(text)
            .into_iter()
            .enumerate()
            .filter_map(|(i, hit)| hit.then_some(i))
            .collect()
    }
}

impl Drop for RegexSet {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { real_set_free(self.handle) }
        }
    }
}

impl Drop for Regex {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { real_free(self.handle) } // null when a fallback backend is in use
        }
    }
}

impl std::fmt::Debug for Regex {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Regex({:?})", self.pattern)
    }
}

impl std::fmt::Display for Regex {
    /// The original pattern — the same affordance as the regex crate.
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_str())
    }
}

impl std::str::FromStr for Regex {
    type Err = Error;

    fn from_str(s: &str) -> Result<Regex, Error> {
        Regex::new(s)
    }
}

// The low-level cursor: yields one match's full span vector at a time.
struct RawSpans<'r, 't> {
    iter: *mut RealIter,           // fast-mode iterator (re's stream); abandoned once we switch to driving
    handle: *const RealRegex,      // for drive mode: re-search from a position with real_find_iter_at
    text: &'t [u8],                // the haystack (drive-mode search pointer + codepoint stepping)
    ngroups: usize,
    buf: Vec<usize>,               // reused span buffer (2*ngroups), refilled per match — never reallocated
    origin: usize,                 // the offset this cursor was created at — where drive mode resumes
                                   // before anything has been yielded (see advance)
    last_end: Option<usize>,       // end of the last YIELDED match — for the empty-adjacent rule
    drive_pos: Option<usize>,      // None = fast mode; Some(p) = driving the search from position p
    utf8: bool,                    // step by one codepoint (str) vs one byte (bytes) past an empty match
    _re: PhantomData<&'r ()>,      // ties the borrowed handle to the Regex's lifetime
}

impl RawSpans<'_, '_> {
    // Advance to the next match, reproducing rust's iteration exactly (regex-automata's util::iter::Searcher).
    // rust DRIVES the search by position: it finds the leftmost match from `input.start`, sets the next start
    // to that match's end, and on an empty match adjacent to the previous end it steps the start forward by
    // one codepoint and re-searches. A filter over REAL's re-ordered stream cannot reproduce this — rust
    // visits positions the re-stream never does (`(?:|ab)*` on "abab": rust yields empties at 1 and 3, which
    // re, advancing by its own wider matches, skips). So we drive too, via real_find_iter_at.
    //
    // But driving allocates an iterator per step, which would undo the span-0 fast path. Since re and rust
    // diverge ONLY at empty matches (a non-empty leftmost match is identical for both, and both advance to its
    // end), we stay on the cheap re-iterator until the FIRST empty match, then switch to driving from rust's
    // current position. Patterns that never match empty (the throughput-critical ones) never switch.
    fn advance(&mut self) -> Option<(usize, usize)> {
        if self.drive_pos.is_some() {
            return self.drive_advance();
        }
        loop {
            let got = unsafe { real_iter_next(self.iter, self.buf.as_mut_ptr()) };
            match got {
                0 => return None,
                // -1 is an internal engine error (or a null cursor). A linear search never "fails to match" —
                // the rust contract is compile -> Result, then matching is infallible — so we surface it.
                -1 => panic!("real-regex: engine iteration failed"),
                _ => {
                    let (s0, e0) = (self.buf[0], self.buf[1]); // group 0 always participates
                    if s0 == e0 {
                        // First empty match: re and rust's advancement diverge here. Switch to driving the
                        // search by position, resuming from rust's current start — the last yielded end, or,
                        // if nothing has been yielded yet, the offset this cursor STARTED at. Resuming from 0
                        // instead sent every `_at` search back to the top of the haystack whenever the match
                        // at `start` was empty: `find_at("x*", "ab", 2)` answered (0,0) where the leftmost
                        // match from 2 is the empty one at 2. Only the empty case was wrong, because only the
                        // empty case takes this branch.
                        self.drive_pos = Some(self.last_end.unwrap_or(self.origin));
                        return self.drive_advance();
                    }
                    self.last_end = Some(e0);
                    return Some((s0, e0));
                }
            }
        }
    }

    // The leftmost match at or after `pos` (unanchored), filling `buf` with its groups. Each call spins up a
    // one-shot iterator — only reached in drive mode, i.e. for empty-capable patterns.
    fn search_at(&mut self, pos: usize) -> Option<(usize, usize)> {
        if pos > self.text.len() {
            return None;
        }
        let it = unsafe {
            real_find_iter_at(self.handle, self.text.as_ptr() as *const c_char, self.text.len(), pos)
        };
        assert!(!it.is_null(), "real-regex: engine iteration failed");
        let got = unsafe { real_iter_next(it, self.buf.as_mut_ptr()) };
        unsafe { real_iter_free(it) };
        match got {
            0 => None,
            -1 => panic!("real-regex: engine iteration failed"),
            _ => Some((self.buf[0], self.buf[1])),
        }
    }

    // Bytes to step past position `pos` when skipping an empty match — one codepoint in str mode (so the next
    // search stays on a char boundary, as rust's UTF-8 Input does), one byte in bytes mode.
    fn step_len(&self, pos: usize) -> usize {
        if !self.utf8 || pos >= self.text.len() {
            return 1;
        }
        match self.text[pos] {
            b if b < 0x80 => 1,
            b if b < 0xE0 => 2,
            b if b < 0xF0 => 3,
            _ => 4,
        }
    }

    // One step of rust's position-driven iteration: find from drive_pos; if that match is empty and adjacent
    // to the previous yielded end, step forward one codepoint and re-search once (handle_overlapping_empty_
    // match); then yield it and set the next start to its end.
    fn drive_advance(&mut self) -> Option<(usize, usize)> {
        let pos = self.drive_pos.expect("drive_advance in fast mode");
        let mut m = self.search_at(pos)?;
        if m.0 == m.1 && Some(m.1) == self.last_end {
            let next = m.1 + self.step_len(m.1);
            m = self.search_at(next)?;
        }
        self.last_end = Some(m.1);
        self.drive_pos = Some(m.1);
        Some(m)
    }

}

impl Drop for RawSpans<'_, '_> {
    fn drop(&mut self) {
        unsafe { real_iter_free(self.iter) }
    }
}

// Unifies the two backends behind one span stream: REAL's cursor (with the empty-match filter) or, under the
// fallback feature, the regex crate's capture iterator (already rust-correct, converted to span vectors).
enum SpanCursor<'r, 't> {
    Real(RawSpans<'r, 't>),
    #[cfg(feature = "fallback")]
    Fallback {
        it: regex::CaptureMatches<'r, 't>,
        ngroups: usize,
        min_start: usize,
        cur: Vec<Option<(usize, usize)>>, // current match's groups, reused across advances
    },
}

impl SpanCursor<'_, '_> {
    // Advance to the next match; return its whole-match span (group 0). The group slots are then available
    // via slot_store() / write_slots() — for the Real backend straight out of the reused flat buffer, so
    // find_iter / is_match / split touch no group storage at all; only captures_iter builds a Captures.
    fn advance(&mut self) -> Option<(usize, usize)> {
        match self {
            SpanCursor::Real(r) => r.advance(),
            #[cfg(feature = "fallback")]
            SpanCursor::Fallback { it, ngroups, min_start, cur } => loop {
                let caps = it.next()?;
                let m0 = caps.get(0).unwrap();
                if m0.start() < *min_start {
                    continue; // for the *_at variants: skip matches before the requested start
                }
                cur.clear();
                cur.extend((0..*ngroups).map(|g| caps.get(g).map(|m| (m.start(), m.end()))));
                return Some((m0.start(), m0.end()));
            },
        }
    }

    // Number of capture slots this cursor reports per match (2 per group, group 0 included).
    fn nslots(&self) -> usize {
        match self {
            SpanCursor::Real(r) => 2 * r.ngroups,
            #[cfg(feature = "fallback")]
            SpanCursor::Fallback { ngroups, .. } => 2 * *ngroups,
        }
    }

    // Write the current match's slots (after advance() returned Some) flat into `out`, whose length is
    // nslots(). The Real backend's buffer is already in this representation; the fallback's Option
    // vector is mapped onto it. The one place either shape is converted.
    fn write_slots(&self, out: &mut [usize]) {
        match self {
            SpanCursor::Real(r) => out.copy_from_slice(&r.buf),
            #[cfg(feature = "fallback")]
            SpanCursor::Fallback { cur, .. } => {
                for (g, s) in cur.iter().enumerate() {
                    let (a, b) = s.unwrap_or((usize::MAX, usize::MAX));
                    out[2 * g] = a;
                    out[(2 * g) + 1] = b;
                }
            }
        }
    }

    // The current match's slots as an owned, inline-when-it-fits store — what a Captures carries.
    fn slot_store(&self) -> SlotStore {
        match self {
            // Fast path: the engine's buffer is already flat, so this is one copy and no conversion.
            SpanCursor::Real(r) => SlotStore::from_flat(&r.buf),
            #[cfg(feature = "fallback")]
            SpanCursor::Fallback { .. } => {
                let n = self.nslots();
                if n <= CAPS_INLINE_SLOTS {
                    let mut slots = [usize::MAX; CAPS_INLINE_SLOTS];
                    self.write_slots(&mut slots[..n]);
                    SlotStore::Inline { len: n as u8, slots }
                } else {
                    let mut v = vec![usize::MAX; n];
                    self.write_slots(&mut v);
                    SlotStore::Spilled(v.into_boxed_slice())
                }
            }
        }
    }

    // Copy the current match's flat slots into a reusable CaptureLocations (no alloc).
    fn copy_slots_into(&self, locs: &mut CaptureLocations) {
        let ngroups = self.nslots() / 2;
        locs.ensure(ngroups);
        self.write_slots(&mut locs.slots);
    }
}

/// Reusable capture-slot buffer — drop-in for [`regex::CaptureLocations`].
///
/// Obtain via [`Regex::capture_locations`], refill with [`Regex::captures_read`] (or
/// [`captures_read_at`](Regex::captures_read_at)). Spans are read with [`get`](CaptureLocations::get).
/// The buffer is not tied to a text lifetime, so it can be reused across many subjects without
/// allocating a [`Captures`] (or a group vector) per match.
#[derive(Clone, Debug)]
pub struct CaptureLocations {
    slots: Vec<usize>, // flat [start0, end0, …]; usize::MAX marks an unset group
    ngroups: usize,
}

impl CaptureLocations {
    /// Number of capture slots, including group 0.
    pub fn len(&self) -> usize {
        self.ngroups
    }

    /// Whether there are no capture slots (never true for a live `Regex`).
    pub fn is_empty(&self) -> bool {
        self.ngroups == 0
    }

    /// Byte offsets `(start, end)` of group `i`, or `None` if the group did not participate
    /// (or `i` is out of range).
    pub fn get(&self, i: usize) -> Option<(usize, usize)> {
        if i >= self.ngroups {
            return None;
        }
        let a = self.slots[2 * i];
        let b = self.slots[2 * i + 1];
        if a == usize::MAX {
            None
        } else {
            Some((a, b))
        }
    }

    fn ensure(&mut self, ngroups: usize) {
        if self.ngroups != ngroups || self.slots.len() != 2 * ngroups {
            self.slots.resize(2 * ngroups, 0);
            self.ngroups = ngroups;
        }
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
    slots: SlotStore,
    groups: Arc<GroupInfo>,
}

impl<'t> Captures<'t> {
    /// Capture group `i` (0 = the whole match), or `None` if it did not participate.
    pub fn get(&self, i: usize) -> Option<Match<'t>> {
        self.slots.group(i).map(|(s, e)| Match { text: self.text, start: s, end: e })
    }

    /// The named capture group `name`, or `None` if it is absent or did not participate.
    pub fn name(&self, name: &str) -> Option<Match<'t>> {
        self.groups.by_name.get(name).and_then(|&i| self.get(i))
    }

    /// The number of capture slots, including group 0.
    pub fn len(&self) -> usize {
        self.slots.ngroups()
    }

    /// Whether there are no capture slots (never true for a real match — group 0 always exists).
    pub fn is_empty(&self) -> bool {
        self.slots.ngroups() == 0
    }

    /// Iterate every group in order (`None` for a group that did not participate).
    pub fn iter(&self) -> impl Iterator<Item = Option<Match<'t>>> + '_ {
        (0..self.len()).map(move |i| self.get(i))
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
    raw: SpanCursor<'r, 't>,
    text: &'t str,
}

impl<'t> Iterator for Matches<'_, 't> {
    type Item = Match<'t>;
    fn next(&mut self) -> Option<Match<'t>> {
        self.raw.advance().map(|(a, b)| Match { text: self.text, start: a, end: b })
    }
}

/// Iterator over capture groups, from [`Regex::captures_iter`].
pub struct CaptureMatches<'r, 't> {
    raw: SpanCursor<'r, 't>,
    re: &'r Regex,
    text: &'t str,
}

/// Iterator over matches with reusable group slots — from [`Regex::captures_read_iter`].
///
/// After each [`next`](Iterator::next) that returns `Some`, the current match's groups are
/// available via [`get`](CaptureLocationMatches::get) without allocating a [`Captures`].
pub struct CaptureLocationMatches<'r, 't> {
    raw: SpanCursor<'r, 't>,
    text: &'t str,
    ngroups: usize,
}

impl CaptureLocationMatches<'_, '_> {
    /// Number of capture slots (including group 0).
    pub fn len(&self) -> usize {
        self.ngroups
    }

    /// Whether there are no capture slots.
    pub fn is_empty(&self) -> bool {
        self.ngroups == 0
    }

    /// Group `i` of the **current** match (after `next` returned `Some`), or `None` if unset /
    /// out of range.
    pub fn get(&self, i: usize) -> Option<(usize, usize)> {
        if i >= self.ngroups {
            return None;
        }
        match &self.raw {
            SpanCursor::Real(r) => {
                let a = r.buf[2 * i];
                let b = r.buf[2 * i + 1];
                if a == usize::MAX {
                    None
                } else {
                    Some((a, b))
                }
            }
            #[cfg(feature = "fallback")]
            SpanCursor::Fallback { cur, .. } => cur.get(i).copied().flatten(),
        }
    }

    /// Copy the current match's spans into `locs` (reusable across subjects / steps).
    pub fn read_captures(&self, locs: &mut CaptureLocations) {
        self.raw.copy_slots_into(locs);
    }
}

impl<'t> Iterator for CaptureLocationMatches<'_, 't> {
    type Item = Match<'t>;
    fn next(&mut self) -> Option<Match<'t>> {
        let (a, b) = self.raw.advance()?;
        Some(Match {
            text: self.text,
            start: a,
            end: b,
        })
    }
}

impl<'t> Iterator for CaptureMatches<'_, 't> {
    type Item = Captures<'t>;
    fn next(&mut self) -> Option<Captures<'t>> {
        self.raw.advance().map(|_| self.re.caps_from(self.text, &self.raw))
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
    #[cfg(feature = "fallback")]
    fallback: bool,
}

impl RegexBuilder {
    /// Start building from `pattern`.
    pub fn new(pattern: &str) -> RegexBuilder {
        RegexBuilder {
            pattern: pattern.to_string(),
            flags: 0,
            #[cfg(feature = "fallback")]
            fallback: false,
        }
    }

    fn set(&mut self, bit: u32, yes: bool) -> &mut RegexBuilder {
        if yes { self.flags |= bit } else { self.flags &= !bit }
        self
    }

    /// Delegate this pattern to the regex crate if REAL cannot run it linearly (requires the `fallback`
    /// feature). Off by default — the crate stays strict. A delegated pattern reports
    /// [`Engine::Fallback`](crate::Engine) and forfeits the linear-time guarantee.
    #[cfg(feature = "fallback")]
    pub fn fallback(&mut self, yes: bool) -> &mut RegexBuilder {
        self.fallback = yes;
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
    pub fn build(&self) -> Result<Regex, Error> {
        match Regex::with_flags(&self.pattern, self.flags) {
            Ok(re) => Ok(re),
            Err(e) => {
                #[cfg(feature = "fallback")]
                if self.fallback && e.is_unsupported() {
                    return Regex::build_fallback(&self.pattern, self.flags);
                }
                Err(e)
            }
        }
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
    use super::{
        compile_handle, real_find_iter, real_find_iter_at, real_free, CaptureLocations, Error,
        GroupInfo, RawSpans, RealRegex, SlotStore, FLAG_ASCII, FLAG_DOTALL, FLAG_ICASE,
        FLAG_MULTILINE, FLAG_VERBOSE,
    };
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
        pub fn new(pattern: &str) -> Result<Regex, Error> {
            Regex::with_flags(pattern.as_bytes(), 0)
        }

        /// Compile a raw-byte pattern with extra `real::flags` (byte mode is always on).
        pub fn with_flags(pattern: &[u8], flags: u32) -> Result<Regex, Error> {
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
            // A null cursor means the engine failed to construct the iterator (never dereference it).
            assert!(!iter.is_null(), "real-regex: engine iteration failed");
            RawSpans { iter, handle: self.handle, text, ngroups: self.ngroups, buf: vec![0usize; 2 * self.ngroups], origin: start.unwrap_or(0), last_end: None, drive_pos: None, utf8: false, _re: PhantomData }
        }

        fn caps_from<'t>(&self, text: &'t [u8], raw: &RawSpans<'_, '_>) -> Captures<'t> {
            // RawSpans::buf is already the flat [start0, end0, …] representation, so this is one copy.
            Captures { text, slots: SlotStore::from_flat(&raw.buf), groups: Arc::clone(&self.groups) }
        }

        /// Whether the pattern matches anywhere in `text`.
        pub fn is_match(&self, text: &[u8]) -> bool {
            self.raw(text, None).advance().is_some()
        }

        /// The leftmost whole match, or `None`.
        pub fn find<'t>(&self, text: &'t [u8]) -> Option<Match<'t>> {
            self.raw(text, None).advance().map(|(a, b)| Match { text, start: a, end: b })
        }

        /// Like [`find`](Regex::find), searching from byte offset `start`.
        pub fn find_at<'t>(&self, text: &'t [u8], start: usize) -> Option<Match<'t>> {
            self.raw(text, Some(start)).advance().map(|(a, b)| Match { text, start: a, end: b })
        }

        /// Iterate whole matches.
        pub fn find_iter<'r, 't>(&'r self, text: &'t [u8]) -> Matches<'r, 't> {
            Matches { raw: self.raw(text, None), text }
        }

        /// Whether the pattern matches at or after byte offset `start`.
        pub fn is_match_at(&self, text: &[u8], start: usize) -> bool {
            self.raw(text, Some(start)).advance().is_some()
        }

        /// The capture groups of the leftmost match, or `None`.
        pub fn captures<'t>(&self, text: &'t [u8]) -> Option<Captures<'t>> {
            {
                let mut c = self.raw(text, None);
                c.advance().map(|_| self.caps_from(text, &c))
            }
        }

        /// Like [`captures`](Regex::captures), searching from byte offset `start`.
        pub fn captures_at<'t>(&self, text: &'t [u8], start: usize) -> Option<Captures<'t>> {
            {
                let mut c = self.raw(text, Some(start));
                c.advance().map(|_| self.caps_from(text, &c))
            }
        }

        /// Reusable capture-slot buffer — see [`crate::Regex::capture_locations`].
        pub fn capture_locations(&self) -> CaptureLocations {
            CaptureLocations {
                slots: vec![0; 2 * self.ngroups],
                ngroups: self.ngroups,
            }
        }

        /// Fill `locs` with the leftmost match's groups (no per-match allocation).
        pub fn captures_read<'t>(
            &self,
            locs: &mut CaptureLocations,
            text: &'t [u8],
        ) -> Option<Match<'t>> {
            self.captures_read_at(locs, text, 0)
        }

        /// Like [`captures_read`](Regex::captures_read), searching from byte offset `start`.
        pub fn captures_read_at<'t>(
            &self,
            locs: &mut CaptureLocations,
            text: &'t [u8],
            start: usize,
        ) -> Option<Match<'t>> {
            locs.ensure(self.ngroups);
            let mut c = self.raw(text, if start == 0 { None } else { Some(start) });
            let (a, b) = c.advance()?;
            locs.slots.copy_from_slice(&c.buf);
            Some(Match {
                text,
                start: a,
                end: b,
            })
        }

        /// Iterate matches without a per-match `Captures` — see [`crate::Regex::captures_read_iter`].
        pub fn captures_read_iter<'r, 't>(
            &'r self,
            text: &'t [u8],
        ) -> CaptureLocationMatches<'r, 't> {
            CaptureLocationMatches {
                raw: self.raw(text, None),
                text,
                ngroups: self.ngroups,
            }
        }

        /// Iterate the capture groups of each match.
        pub fn captures_iter<'r, 't>(&'r self, text: &'t [u8]) -> CaptureMatches<'r, 't> {
            CaptureMatches { raw: self.raw(text, None), re: self, text }
        }

        /// The end offset of the leftmost match. Same divergence as the string API's
        /// [`shortest_match`](crate::Regex::shortest_match) — the leftmost match's greedy end.
        pub fn shortest_match(&self, text: &[u8]) -> Option<usize> {
            self.raw(text, None).advance().map(|(_, e)| e)
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

        /// Like [`split`](Regex::split), but yielding at most `limit` pieces (the last is the unsplit
        /// remainder). `limit == 0` yields nothing.
        pub fn splitn<'r, 't>(&'r self, text: &'t [u8], limit: usize) -> SplitN<'r, 't> {
            SplitN { inner: self.split(text), limit, n: 0 }
        }
    }

    impl std::fmt::Display for Regex {
        /// The original pattern — the same affordance as the regex crate.
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            match std::str::from_utf8(&self.pattern) {
                Ok(s) => f.write_str(s),
                Err(_) => f.write_str(&String::from_utf8_lossy(&self.pattern)),
            }
        }
    }

    impl std::str::FromStr for Regex {
        type Err = Error;

        fn from_str(s: &str) -> Result<Regex, Error> {
            Regex::new(s)
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
        slots: SlotStore,
        groups: Arc<GroupInfo>,
    }

    impl<'t> Captures<'t> {
        /// Capture group `i` (0 = whole match).
        pub fn get(&self, i: usize) -> Option<Match<'t>> {
            self.slots.group(i).map(|(s, e)| Match { text: self.text, start: s, end: e })
        }
        /// The named capture group `name`.
        pub fn name(&self, name: &str) -> Option<Match<'t>> {
            self.groups.by_name.get(name).and_then(|&i| self.get(i))
        }
        /// The number of capture slots (incl. group 0).
        pub fn len(&self) -> usize { self.slots.ngroups() }
        /// Whether there are no slots (never for a real match).
        pub fn is_empty(&self) -> bool { self.slots.ngroups() == 0 }
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
            self.raw.advance().map(|(a, b)| Match { text: self.text, start: a, end: b })
        }
    }

    /// Iterator over capture groups.
    pub struct CaptureMatches<'r, 't> {
        raw: RawSpans<'r, 't>,
        re: &'r Regex,
        text: &'t [u8],
    }

    /// Iterator over matches with reusable group slots — from [`Regex::captures_read_iter`].
    pub struct CaptureLocationMatches<'r, 't> {
        raw: RawSpans<'r, 't>,
        text: &'t [u8],
        ngroups: usize,
    }

    impl CaptureLocationMatches<'_, '_> {
        /// Number of capture slots (including group 0).
        pub fn len(&self) -> usize {
            self.ngroups
        }

        /// Group `i` of the current match, or `None` if unset / out of range.
        pub fn get(&self, i: usize) -> Option<(usize, usize)> {
            if i >= self.ngroups {
                return None;
            }
            let a = self.raw.buf[2 * i];
            let b = self.raw.buf[2 * i + 1];
            if a == usize::MAX {
                None
            } else {
                Some((a, b))
            }
        }

        /// Copy the current match into `locs`.
        pub fn read_captures(&self, locs: &mut CaptureLocations) {
            locs.ensure(self.ngroups);
            locs.slots.copy_from_slice(&self.raw.buf);
        }
    }

    impl<'t> Iterator for CaptureLocationMatches<'_, 't> {
        type Item = Match<'t>;
        fn next(&mut self) -> Option<Match<'t>> {
            let (a, b) = self.raw.advance()?;
            Some(Match {
                text: self.text,
                start: a,
                end: b,
            })
        }
    }

    impl<'t> Iterator for CaptureMatches<'_, 't> {
        type Item = Captures<'t>;
        fn next(&mut self) -> Option<Captures<'t>> {
            self.raw.advance().map(|_| self.re.caps_from(self.text, &self.raw))
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

    /// Iterator of at most `limit` pieces, from [`Regex::splitn`].
    pub struct SplitN<'r, 't> {
        inner: Split<'r, 't>,
        limit: usize,
        n: usize,
    }

    impl<'t> Iterator for SplitN<'_, 't> {
        type Item = &'t [u8];
        fn next(&mut self) -> Option<&'t [u8]> {
            if self.n >= self.limit {
                return None;
            }
            self.n += 1;
            if self.n == self.limit {
                if self.inner.done {
                    return None;
                }
                self.inner.done = true;
                return Some(&self.inner.text[self.inner.last..]);
            }
            self.inner.next()
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
        pub fn build(&self) -> Result<Regex, Error> {
            Regex::with_flags(&self.pattern, self.flags)
        }
    }
}
