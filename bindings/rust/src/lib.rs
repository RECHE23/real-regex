//! Linear-time, ReDoS-safe regular expressions with bounded lookarounds — safe Rust bindings to the REAL
//! C++ engine (via its C ABI). Every pattern accepted here matches in time linear in the input: there is no
//! backtracking, so no catastrophic blow-up. The engine is **strict by design** — a construct it cannot run
//! linearly (a backreference, an unbounded lookaround) is rejected at [`Regex::new`], never silently made
//! non-linear.
//!
//! ```
//! use real_regex::Regex;
//! let re = Regex::new(r"(\w+)@(\w+)").unwrap();
//! let m = re.find_iter("a@b cd@ef").next().unwrap();
//! assert_eq!(m.get(1), Some("a"));
//! assert_eq!(m.get(2), Some("b"));
//! ```
use std::marker::PhantomData;
use std::os::raw::c_char;

// Opaque C handles.
enum RealRegex {}
enum RealIter {}

extern "C" {
    fn real_compile(pattern: *const c_char, len: usize, flags: u32,
                    errbuf: *mut c_char, errbuf_len: usize) -> *mut RealRegex;
    fn real_group_count(re: *const RealRegex) -> usize;
    fn real_free(re: *mut RealRegex);
    fn real_find_iter(re: *const RealRegex, text: *const c_char, len: usize) -> *mut RealIter;
    fn real_iter_next(iter: *mut RealIter, spans: *mut usize) -> i32;
    fn real_iter_free(iter: *mut RealIter);
}

/// A compiled pattern.
pub struct Regex {
    handle: *mut RealRegex,
    ngroups: usize, // capture slots per match, including group 0
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
    /// ascii=64).
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
        Ok(Regex { handle, ngroups })
    }

    /// The number of capture groups, excluding the whole-match group 0.
    pub fn captures_len(&self) -> usize {
        self.ngroups - 1
    }

    /// Iterate the non-overlapping matches in `text`, in order.
    pub fn find_iter<'r, 't>(&'r self, text: &'t str) -> Matches<'r, 't> {
        let iter = unsafe {
            real_find_iter(self.handle, text.as_ptr() as *const c_char, text.len())
        };
        Matches { iter, ngroups: self.ngroups, text, _re: PhantomData }
    }

    /// Whether the pattern matches anywhere in `text`.
    pub fn is_match(&self, text: &str) -> bool {
        self.find_iter(text).next().is_some()
    }
}

impl Drop for Regex {
    fn drop(&mut self) {
        unsafe { real_free(self.handle) }
    }
}

/// One match: the whole match (group 0) plus each capture group's slice, borrowed from the subject.
pub struct Match<'t> {
    spans: Vec<Option<(usize, usize)>>,
    text: &'t str,
}

impl<'t> Match<'t> {
    /// The slice of group `i` (0 = the whole match), or `None` if the group did not participate.
    pub fn get(&self, i: usize) -> Option<&'t str> {
        self.spans.get(i).copied().flatten().map(|(a, b)| &self.text[a..b])
    }

    /// The `(start, end)` byte span of group `i`, or `None` if it did not participate.
    pub fn span(&self, i: usize) -> Option<(usize, usize)> {
        self.spans.get(i).copied().flatten()
    }

    /// The number of capture slots (including group 0).
    pub fn len(&self) -> usize {
        self.spans.len()
    }

    /// Whether there are no capture slots at all (never true for a valid match — group 0 always exists).
    pub fn is_empty(&self) -> bool {
        self.spans.is_empty()
    }
}

/// The iterator returned by [`Regex::find_iter`].
pub struct Matches<'r, 't> {
    iter: *mut RealIter,
    ngroups: usize,
    text: &'t str,
    _re: PhantomData<&'r Regex>,
}

impl<'t> Iterator for Matches<'_, 't> {
    type Item = Match<'t>;

    fn next(&mut self) -> Option<Match<'t>> {
        let mut spans = vec![0usize; 2 * self.ngroups];
        let got = unsafe { real_iter_next(self.iter, spans.as_mut_ptr()) };
        if got == 0 {
            return None;
        }
        let groups = (0..self.ngroups)
            .map(|g| {
                let (a, b) = (spans[2 * g], spans[2 * g + 1]);
                if a == usize::MAX { None } else { Some((a, b)) }
            })
            .collect();
        Some(Match { spans: groups, text: self.text })
    }
}

impl Drop for Matches<'_, '_> {
    fn drop(&mut self) {
        unsafe { real_iter_free(self.iter) }
    }
}
