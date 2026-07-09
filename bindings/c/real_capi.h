/*
 * real_capi.h — a minimal C ABI over the REAL regex engine.
 *
 * Opaque handles, byte-oriented (the caller owns the pattern/text buffers). It is the substrate the Rust
 * crate (and any future Node/Ruby binding) builds on. Not the C++ API: for C++, #include <real/real.hpp>.
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

/* Advance to the next match. On a match (return 1), fills `spans` with 2 * real_group_count(re) offsets
 * (start0, end0, start1, end1, …); an unset group is (SIZE_MAX, SIZE_MAX). Returns 0 at the end of iteration,
 * and -1 on an internal engine error or a NULL iterator (no C++ exception ever crosses this boundary). */
int real_iter_next(real_iter* iter, size_t* spans);

void real_iter_free(real_iter* iter);

/* Count non-overlapping matches in [text, text+len) without materialising match objects
 * (matching-only; takes the trailing-LA class+ fast path when eligible). Returns the count, or
 * (size_t)-1 on error / null re. */
size_t real_count_matches(const real_regex* re, const char* text, size_t len);

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
