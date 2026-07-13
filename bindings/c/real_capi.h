/*
 * real_capi.h — a minimal C ABI over the REAL regex engine.
 *
 * Opaque handles, byte-oriented (the caller owns the pattern/text buffers). It is the substrate the Rust
 * crate (and any future Go/Node/Ruby binding) builds on. Not the C++ API: for C++, #include <real/real.hpp>.
 *
 * Thread safety: every function taking a `const real_regex*` / `const real_regex_set*` only reads it — a
 * single compiled pattern (or set) may be shared across threads and matched against concurrently, with no
 * external locking. Each call allocates its own scratch (VM state, capture slots) internally; nothing is
 * cached on the handle. `real_iter`/`real_iter_next` are the one exception: an iterator is a cursor with
 * mutable position, so a single `real_iter*` must not be driven from more than one thread at a time (a
 * fresh iterator per thread is fine, from the same `real_regex*`).
 *
 * Error convention, stated once for the whole header: every function that can fail returns a sentinel —
 * NULL for a handle-returning function, -1 (or (size_t)-1 where the return is a count/length) otherwise —
 * and nothing here holds error state between calls (no `errno`-style global, no "last error" handle method:
 * a compile failure's message goes straight to the caller's own `errbuf` in the same call). No C++
 * exception ever crosses this boundary; an internal engine error surfaces as the same failure sentinel a
 * bad input would, never a crash or a propagated throw.
 *
 * Flag numbering does NOT match Python's `re` module — a binding author assuming otherwise (the most likely
 * first mistake porting from the Python binding) will silently compile with the wrong mode. REAL's native
 * bitmask (this header's `flags` parameter) and Python's `re`-style numbering diverge on every shared flag,
 * and REAL has three flags Python's `re` has no equivalent for:
 *
 *   meaning                            | REAL native (this header) | Python `re` (`import re`)
 *   -----------------------------------|----------------------------|---------------------------
 *   none                                | 0                          | 0
 *   icase       (`re.I`)                | 1                          | 2
 *   multiline   (`re.M`)                | 2                          | 8
 *   dotall      (`re.S`)                | 4                          | 16
 *   bytes       (binary mode)           | 8                          | — (no equivalent)
 *   verbose     (`re.X`)                | 16                         | 64
 *   ecma        (JS `$`/`.` semantics)  | 32                         | — (no equivalent)
 *   ascii       (`re.A`)                | 64                         | 256
 *   dollar_endonly (Rust `$`/`\z`)      | 128                        | — (no equivalent)
 *
 * A binding exposing Python-style flag constants to its own users must translate between the two, never
 * pass Python's numeric value straight through to `flags` here.
 */
#ifndef REAL_CAPI_H
#define REAL_CAPI_H

#include <stddef.h>
#include <stdint.h> /* uint8_t, uint32_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct real_regex real_regex;   /* a compiled pattern */
typedef struct real_iter  real_iter;    /* a match cursor over one subject */

/* Error classification written to real_compile's `code` out-param: a stable, machine-readable tag so a
 * binding classifies a rejection without matching on the message text. */
enum {
  REAL_ERR_NONE = 0,        /* success */
  REAL_ERR_SYNTAX = 1,      /* the pattern is malformed */
  REAL_ERR_UNSUPPORTED = 2  /* well-formed but beyond REAL's linear engine (backreference, \p{...}, …) */
};

/* Compile [pattern, pattern+len). `flags` is a bitmask of real::flags (icase=1, multiline=2, dotall=4,
 * bytes=8, verbose=16, ecma=32, ascii=64). Returns NULL on error, filling `errbuf` (NUL-terminated) and (if
 * non-NULL) `code` with one of the REAL_ERR_* values. The returned handle must be freed with real_free. */
real_regex* real_compile(const char* pattern, size_t len, uint32_t flags,
                         char* errbuf, size_t errbuf_len, int* code);

/* Number of capture-span slots per match: (capturing groups + 1) for group 0. The `spans` buffer passed to
 * real_iter_next must hold 2 * this many size_t. */
size_t real_group_count(const real_regex* re);

/* Write the name of capture group `group` (NUL-terminated) into `buf`, and return the name's length. A group
 * with no name (including group 0) writes an empty string and returns 0. `buf` may be NULL to query the
 * length only. */
size_t real_group_name(const real_regex* re, size_t group, char* buf, size_t buflen);

void real_free(real_regex* re);

/* Iterate the non-overlapping matches over [text, text+len). Both `re` and the text buffer must outlive the
 * returned iterator. Returns NULL if the iterator could not be constructed (allocation failure, or the engine
 * reporting an internal error) — the caller MUST check for NULL and must not call real_iter_next on it. */
real_iter* real_find_iter(const real_regex* re, const char* text, size_t len);

/* Like real_find_iter, but the search starts at byte offset `start` (the region [start, len)). Anchors see
 * `start` as the region start, matching the engine's pos semantics. Same NULL contract as real_find_iter. */
real_iter* real_find_iter_at(const real_regex* re, const char* text, size_t len, size_t start);

/* Like real_find_iter, but bounded on both sides: matches are found within [start, end) of [text, len)
 * (Python `finditer`'s pos/endpos). `end` truncates the subject to a view (`$`/`\Z`/a trailing `\b` see it
 * as the end); `start` is the scan's anchor, not a slice (same pos semantics as real_find_iter_at — `\A`/`^`
 * without MULTILINE still fail at start > 0). `end` is clamped to len; start > end yields an immediately
 * exhausted iterator, not an error. A separate function rather than a real_find_iter_at overload — this ABI
 * has no default arguments. Same NULL contract as real_find_iter. */
real_iter* real_find_iter_between(const real_regex* re, const char* text, size_t len, size_t start, size_t end);

/* Advance to the next match. On a match (return 1), fills `spans` with 2 * real_group_count(re) offsets
 * (start0, end0, start1, end1, …); an unset group is (SIZE_MAX, SIZE_MAX). Returns 0 at the end of iteration,
 * and -1 on an internal engine error or a NULL iterator (no C++ exception ever crosses this boundary). */
int real_iter_next(real_iter* iter, size_t* spans);

void real_iter_free(real_iter* iter);

/* Count non-overlapping matches in [text, text+len) without materialising match objects
 * (matching-only; takes the trailing-LA class+ fast path when eligible). Returns the count, or
 * (size_t)-1 on error / null re. */
size_t real_count_matches(const real_regex* re, const char* text, size_t len);

/* Single-shot anchored match attempt: exposes the engine's three run modes (search/match/fullmatch) as ONE
 * function rather than three, mirroring real::regex::search/match/fullmatch's own region-aware C++
 * overloads. `mode` is one of the REAL_MODE_* values below. `start` is the VM's anchor position, not a
 * slice: `\A`/`^` without MULTILINE still fail at start > 0, exactly as in the C++/Python APIs. `end`
 * truncates the subject to a view (clamped to len); start > end yields no match, not an error. On a match
 * (return 1), `spans` is filled exactly like real_iter_next's. Returns 0 on no match, -1 on error (NULL
 * re/text, or an internal engine error). */
enum {
  REAL_MODE_SEARCH = 0,   /* leftmost match anywhere in the region (Python re.search) */
  REAL_MODE_MATCH = 1,    /* anchored at `start` (Python re.match) */
  REAL_MODE_FULLMATCH = 2 /* the whole region [start, end) must match (Python re.fullmatch) */
};
int real_match(const real_regex* re, const char* text, size_t len,
              size_t start, size_t end, int mode, size_t* spans);

/* Applies a Python re-compatible replacement template across the non-overlapping matches of `re` in
 * [text, text+len), replacing up to `count` of them (0 = all, matching re.sub's default; no pos/endpos —
 * re.sub has none either). Template syntax: \1..\<N> and \g<n>/\g<name> group references, \n \t \r \f \v \a
 * \\ escapes, and \0..\7-led octal byte escapes (always ONE raw byte here — this ABI is byte-oriented, see
 * the file header; contrast the Python binding, which re-encodes a high octal value as UTF-8 in str mode).
 * An unmatched OPTIONAL group referenced by the template contributes nothing (re's own rule), not an error.
 *
 * Two-call convention, like real_group_name: call once with `out == NULL` to size (the return value is the
 * result's length in bytes — no NUL is added, the result may itself contain embedded NULs); allocate a
 * buffer of that length; call again with `out` pointing at it (and `outlen` >= that length) to fill it.
 * `text`/`repl`/`count` must be identical between the two calls — the second call recomputes the whole
 * result rather than resuming the first, so a change would silently size and fill against two different
 * answers. `n_subs`, if non-NULL, receives the number of replacements performed, set on EITHER call.
 * Returns (size_t)-1 on error (bad template syntax, an unknown or out-of-range group reference, or a NULL
 * re/text/repl) with `errbuf` filled — never a crossed C++ exception. */
size_t real_sub(const real_regex* re, const char* text, size_t len,
                const char* repl, size_t repl_len, size_t count,
                char* out, size_t outlen, size_t* n_subs,
                char* errbuf, size_t errbuf_len);

/* --- multi-pattern set (which-matched; Stage-1 N-walks) --------------------- */

typedef struct real_regex_set real_regex_set;

/* Compile N patterns into a set. `patterns[i]` has length `lens[i]`. `flags` applies to every
 * pattern. Fails (NULL) if any pattern is invalid — no silent skip. Bitset order = patterns order. */
real_regex_set* real_set_compile(const char* const* patterns, const size_t* lens, size_t n,
                                 uint32_t flags, char* errbuf, size_t errbuf_len, int* code);

size_t real_set_size(const real_regex_set* set);

void real_set_free(real_regex_set* set);

/* 1 if any pattern matches [text, text+len) at least once; 0 if none; -1 on error. */
int real_set_is_match(const real_regex_set* set, const char* text, size_t len);

/* Which-matched bitset: writes `real_set_size(set)` bytes to `out` (0/1 per pattern, construction
 * order). Returns 0 on success, -1 on error. `out` must hold at least size bytes. */
int real_set_matches(const real_regex_set* set, const char* text, size_t len, uint8_t* out);

#ifdef __cplusplus
}
#endif

#endif /* REAL_CAPI_H */
