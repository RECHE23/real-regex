/*
 * real_capi.h — a minimal C ABI over the REAL regex engine.
 *
 * Opaque handles, byte-oriented (the caller owns the pattern/text buffers). It is the substrate the Rust
 * crate (and any future Node/Ruby binding) builds on. Not the C++ API: for C++, #include <real/real.hpp>.
 */
#ifndef REAL_CAPI_H
#define REAL_CAPI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct real_regex real_regex;   /* a compiled pattern */
typedef struct real_iter  real_iter;    /* a match cursor over one subject */

/* Compile [pattern, pattern+len). `flags` is a bitmask of real::flags (icase=1, multiline=2, dotall=4,
 * bytes=8, verbose=16, ecma=32, ascii=64). Returns NULL on error, filling `errbuf` (NUL-terminated) if
 * given. The returned handle must be freed with real_free. */
real_regex* real_compile(const char* pattern, size_t len, uint32_t flags, char* errbuf, size_t errbuf_len);

/* Number of capture-span slots per match: (capturing groups + 1) for group 0. The `spans` buffer passed to
 * real_iter_next must hold 2 * this many size_t. */
size_t real_group_count(const real_regex* re);

/* Write the name of capture group `group` (NUL-terminated) into `buf`, and return the name's length. A group
 * with no name (including group 0) writes an empty string and returns 0. `buf` may be NULL to query the
 * length only. */
size_t real_group_name(const real_regex* re, size_t group, char* buf, size_t buflen);

void real_free(real_regex* re);

/* Iterate the non-overlapping matches over [text, text+len). Both `re` and the text buffer must outlive the
 * returned iterator. Returns NULL only on allocation failure. */
real_iter* real_find_iter(const real_regex* re, const char* text, size_t len);

/* Like real_find_iter, but the search starts at byte offset `start` (the region [start, len)). Anchors see
 * `start` as the region start, matching the engine's pos semantics. */
real_iter* real_find_iter_at(const real_regex* re, const char* text, size_t len, size_t start);

/* Advance to the next match. On a match, fills `spans` with 2 * real_group_count(re) offsets
 * (start0, end0, start1, end1, …); an unset group is (SIZE_MAX, SIZE_MAX). Returns 1 on a match, 0 at the
 * end. */
int real_iter_next(real_iter* iter, size_t* spans);

void real_iter_free(real_iter* iter);

#ifdef __cplusplus
}
#endif

#endif /* REAL_CAPI_H */
