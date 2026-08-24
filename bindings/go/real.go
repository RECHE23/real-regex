// Package real provides Go bindings to the REAL regex engine (linear-time, ReDoS-safe) via
// cgo, over the C ABI at bindings/c/real_capi.h (v2026.7.39+, frozen-additive — vendored here,
// see vendor_include/ and the Makefile's `vendor`/`check-vendor` targets).
//
// v0.1: a regexp-idiomatic subset (Compile/MustCompile/String/Match/MatchString/Find/FindString/
// FindIndex/FindAll/FindAllString/Split/FindAllIndex/FindSubmatch/FindStringSubmatch/
// FindSubmatchIndex/ReplaceAll/RegexSet)
// plus REAL extensions regexp has no equivalent for at all (FullMatch,
// bounded lookarounds, possessive quantifiers). See README.md for the full method-to-C-ABI
// mapping and documented flavor divergences from Go's regexp (RE2) — most importantly: \w,
// \d, and \s are Unicode-aware by default here (Python re parity), where regexp's are
// ASCII-only by default. That is the single most likely surprise for a regexp migrator.
//
// Cross-compilation and Windows/MSVC are explicitly out of scope for v0 (cgo + a vendored
// C++20 TU; proven on darwin/arm64 and linux/amd64 only).
package real

/*
#cgo darwin CXXFLAGS: -std=c++20 -I${SRCDIR}/vendor_include -O2
#cgo darwin LDFLAGS: -lc++
#cgo linux CXXFLAGS: -std=c++20 -I${SRCDIR}/vendor_include -O2
#cgo linux LDFLAGS: -lstdc++
#include "real_capi.h"
#include <stdlib.h>

// Implemented in engine_version.cpp (Go-binding-internal, NOT real_capi.h/.cpp — that ABI is
// frozen-additive and has no version accessor). A plain extern declaration here, not an
// #include of real/version.hpp: cgo's own preamble-parsing pass runs a C (not C++) front end
// with none of this package's #cgo CXXFLAGS applied, and every real/*.hpp header enforces a
// hard C++20 #error gate -- so the vendored header can only be touched from a real .cpp file,
// compiled normally (and correctly, with -std=c++20) as part of the package build, exactly
// like real_capi.cpp already is.
extern const char* real_go_engine_version(void);
*/
import "C"

import (
	"bytes"
	"errors"
	"regexp"
	"runtime"
	"unsafe"
)

// noCopy may be added to structs which must not be copied after the first use.
// Same shape as sync.noCopy: a go vet -copylocks hook, not a compiler error.
// Must not be embedded — Lock/Unlock would become part of the public API.
type noCopy struct{}

func (*noCopy) Lock()   {}
func (*noCopy) Unlock() {}

// sizeMax is (size_t)-1, the C ABI's error sentinel for count/length-returning functions.
var sizeMax = ^C.size_t(0)

// EngineVersion is the vendored REAL C++ engine's version (e.g. "2026.7.40") — the engine
// version actually compiled into this build. Intentionally decoupled from this module's own
// semver (bindings/go/vX.Y.Z, Go's Semantic Import Versioning) — see README.md's Versioning
// section: a CalVer major (e.g. "2026") baked into the Go import path would force a breaking
// import-path change every year, which is exactly what the tag-prefix scheme avoids.
var EngineVersion = C.GoString(C.real_go_engine_version())

// cBytes returns a pointer usable as a real_capi (text, len) or (repl, repl_len) argument.
// (NULL, 0) is a valid empty subject throughout the C ABI (v2026.7.39+) —
// so an empty slice needs no allocation at all, matching Go's own nil-slice representation.
func cBytes(b []byte) (unsafe.Pointer, func()) {
	if len(b) == 0 {
		return nil, func() {}
	}
	p := C.CBytes(b)
	return p, func() { C.free(p) }
}

// spansToIndices converts a raw spans buffer (real_iter_next/real_match's own [start0,end0,
// start1,end1,...] shape) to Go's regexp convention: -1 for a group that did not participate,
// instead of the C ABI's SIZE_MAX sentinel.
func spansToIndices(spans []C.size_t) []int {
	out := make([]int, len(spans))
	for i, s := range spans {
		if s == sizeMax {
			out[i] = -1
		} else {
			out[i] = int(s)
		}
	}
	return out
}

// Regexp wraps a compiled real_regex handle. Safe for concurrent use by multiple goroutines
// (the C ABI's own thread-safety contract: a const handle only reads). Close releases the
// underlying C++ object; a runtime.SetFinalizer is a safety net, not a substitute for it.
//
// expr is the source text, kept here because the C ABI has no pattern getter. String()
// reads this field; it is not recovered from the handle. A value copy of Regexp still
// shares the C pointer — unlike regexp.Regexp, *r is not a safe clone. noCopy makes
// that copy a go vet -copylocks diagnostic; the compiler will still accept it.
type Regexp struct {
	_    noCopy
	re   *C.real_regex
	expr string
}

// Compile compiles pattern. Mirrors regexp.Compile's signature and error contract.
func Compile(pattern string) (*Regexp, error) {
	cpat, freePat := cBytes([]byte(pattern))
	defer freePat()
	var errbuf [256]C.char
	var code C.int
	h := C.real_compile((*C.char)(cpat), C.size_t(len(pattern)), 0,
		&errbuf[0], C.size_t(len(errbuf)), &code)
	if h == nil {
		return nil, errors.New(C.GoString(&errbuf[0]))
	}
	r := &Regexp{re: h, expr: pattern}
	runtime.SetFinalizer(r, (*Regexp).Close)
	return r, nil
}

// String returns the source text used to compile the expression, like
// regexp.Regexp.String. The text lives on this value; Close does not clear it.
func (r *Regexp) String() string {
	return r.expr
}

// MustCompile is like Compile but panics on error, mirroring regexp.MustCompile.
func MustCompile(pattern string) *Regexp {
	r, err := Compile(pattern)
	if err != nil {
		panic(err)
	}
	return r
}

// Close releases the compiled pattern. Idempotent.
//
// Post-Close contract: the handle is nilled; subsequent method calls must not crash and return
// zero-values (NumSubexp → 0; SubexpNames → empty slice; Find*/FindAll*/FindSubmatchIndex → nil
// or ""; Match/MatchString/FullMatch → false; ReplaceAll → error). Split on a closed handle is
// the no-match path (the whole string). String is the exception: the source text lives on this
// value, not on the handle, so Close leaves it. Prefer not to use a closed Regexp for matching
// — the guarantee is crash-freedom and stable sentinels, not a second valid lifetime.
func (r *Regexp) Close() error {
	if r.re != nil {
		C.real_free(r.re)
		r.re = nil
		runtime.SetFinalizer(r, nil)
	}
	return nil
}

// groupCount returns (capturing groups + 1), matching real_group_count's own contract.
// After Close (nil handle) the C ABI returns 0.
func (r *Regexp) groupCount() int {
	return int(C.real_group_count(r.re))
}

// groupSlots allocates a spans buffer sized for this pattern's group count.
func (r *Regexp) groupSlots() []C.size_t {
	return make([]C.size_t, 2*r.groupCount())
}

// spanPtr returns a C-safe pointer to the first span slot, or nil when the slice is empty
// (closed handle / zero groupCount). Same idiom as RegexSet.Matches — never &x[0] on len 0.
func spanPtr(spans []C.size_t) *C.size_t {
	if len(spans) == 0 {
		return nil
	}
	return &spans[0]
}

// NumSubexp returns the number of capturing groups (excluding group 0), like
// regexp.Regexp.NumSubexp. After Close it returns 0 (not −1).
func (r *Regexp) NumSubexp() int {
	if r.re == nil {
		return 0
	}
	return r.groupCount() - 1
}

// SubexpNames returns each group's name in group-index order; index 0 (the whole match) and
// any unnamed group are "" — the same shape as regexp.Regexp.SubexpNames.
// Names are fetched with the C ABI two-call protocol (length query, then exact buffer) so long
// names are never truncated or read out of bounds.
func (r *Regexp) SubexpNames() []string {
	n := r.groupCount()
	names := make([]string, n)
	for g := 0; g < n; g++ {
		nameLen := C.real_group_name(r.re, C.size_t(g), nil, 0)
		if nameLen == 0 {
			continue
		}
		buf := make([]C.char, nameLen+1)
		C.real_group_name(r.re, C.size_t(g), &buf[0], C.size_t(len(buf)))
		names[g] = C.GoStringN(&buf[0], C.int(nameLen))
	}
	return names
}

// FindAllIndex returns the [start,end) byte-offset pairs of every non-overlapping match in
// text, in order — same shape as regexp.Regexp.FindAllIndex(text, -1) (whole match only,
// group 0; see FindAllSubmatchIndex for every group). Byte offsets throughout — Go's own
// regexp package is byte-oriented too, so there is no char/byte translation layer here at
// all, unlike the Python binding.
func (r *Regexp) FindAllIndex(text []byte) [][]int {
	all := r.FindAllSubmatchIndex(text)
	if all == nil {
		return nil
	}
	out := make([][]int, len(all))
	for i, m := range all {
		out[i] = []int{m[0], m[1]}
	}
	return out
}

// Match reports whether b contains any match of the expression, like regexp.Regexp.Match.
func (r *Regexp) Match(b []byte) bool {
	return r.FindIndex(b) != nil
}

// MatchString reports whether s contains any match of the expression, like
// regexp.Regexp.MatchString. It is a search, not a full-string match — see FullMatch.
func (r *Regexp) MatchString(s string) bool {
	return r.Match([]byte(s))
}

// FindIndex returns the [start,end) of the leftmost match in b, or nil, like
// regexp.Regexp.FindIndex.
func (r *Regexp) FindIndex(b []byte) []int {
	m := r.FindSubmatchIndex(b)
	if m == nil {
		return nil
	}
	return []int{m[0], m[1]}
}

// FindStringIndex is FindIndex on a string.
func (r *Regexp) FindStringIndex(s string) []int {
	return r.FindIndex([]byte(s))
}

// Find returns the leftmost match in b, or nil if there is none — like regexp.Regexp.Find.
// An empty match is a non-nil empty slice.
func (r *Regexp) Find(b []byte) []byte {
	loc := r.FindIndex(b)
	if loc == nil {
		return nil
	}
	return b[loc[0]:loc[1]]
}

// FindString returns the leftmost match in s, or "" if there is none — like
// regexp.Regexp.FindString. An empty match and no match are indistinguishable.
func (r *Regexp) FindString(s string) string {
	m := r.Find([]byte(s))
	if m == nil {
		return ""
	}
	return string(m)
}

// findAllIndexN is FindAllIndex with regexp's n: 0 → nil, <0 → all, >0 → at most n.
func (r *Regexp) findAllIndexN(b []byte, n int) [][]int {
	if n == 0 {
		return nil
	}
	all := r.FindAllIndex(b)
	if all == nil || n < 0 || n >= len(all) {
		return all
	}
	return all[:n]
}

// FindAll returns successive matches in b, like regexp.Regexp.FindAll. n is the cap
// (0 → nil, <0 → all).
func (r *Regexp) FindAll(b []byte, n int) [][]byte {
	idx := r.findAllIndexN(b, n)
	if idx == nil {
		return nil
	}
	out := make([][]byte, len(idx))
	for i, loc := range idx {
		out[i] = b[loc[0]:loc[1]]
	}
	return out
}

// FindAllString is FindAll on a string, like regexp.Regexp.FindAllString.
func (r *Regexp) FindAllString(s string, n int) []string {
	idx := r.findAllIndexN([]byte(s), n)
	if idx == nil {
		return nil
	}
	out := make([]string, len(idx))
	for i, loc := range idx {
		out[i] = s[loc[0]:loc[1]]
	}
	return out
}

// FindAllStringIndex is FindAllIndex with regexp's n, on a string.
func (r *Regexp) FindAllStringIndex(s string, n int) [][]int {
	return r.findAllIndexN([]byte(s), n)
}

// Split slices s at matches of the expression, like regexp.Regexp.Split.
// n == 0 returns nil; n < 0 returns all substrings; n > 0 returns at most n.
func (r *Regexp) Split(s string, n int) []string {
	if n == 0 {
		return nil
	}
	if len(r.expr) > 0 && len(s) == 0 {
		return []string{""}
	}
	matches := r.FindAllStringIndex(s, n)
	parts := make([]string, 0, len(matches))
	beg, end := 0, 0
	for _, match := range matches {
		if n > 0 && len(parts) >= n-1 {
			break
		}
		end = match[0]
		if match[1] != 0 {
			parts = append(parts, s[beg:end])
		}
		beg = match[1]
	}
	if end != len(s) {
		parts = append(parts, s[beg:])
	}
	return parts
}

// submatchBytes slices b at idx (FindSubmatchIndex shape). A group whose start is -1
// stays nil, like regexp.Regexp.FindSubmatch — not an empty slice.
func submatchBytes(b []byte, idx []int) [][]byte {
	if idx == nil {
		return nil
	}
	out := make([][]byte, len(idx)/2)
	for i := range out {
		a, e := idx[2*i], idx[2*i+1]
		if a >= 0 {
			out[i] = b[a:e]
		}
	}
	return out
}

// submatchStrings slices s at idx. A group whose start is -1 stays "", like
// regexp.Regexp.FindStringSubmatch (empty string and unset are indistinguishable).
func submatchStrings(s string, idx []int) []string {
	if idx == nil {
		return nil
	}
	out := make([]string, len(idx)/2)
	for i := range out {
		a, e := idx[2*i], idx[2*i+1]
		if a >= 0 {
			out[i] = s[a:e]
		}
	}
	return out
}

// FindSubmatch returns the leftmost match and its groups as sub-slices of b, or nil —
// like regexp.Regexp.FindSubmatch. An unset group is nil; an empty participating group
// is a non-nil empty slice.
func (r *Regexp) FindSubmatch(b []byte) [][]byte {
	return submatchBytes(b, r.FindSubmatchIndex(b))
}

// FindStringSubmatch is FindSubmatch on a string, like regexp.Regexp.FindStringSubmatch.
// No match is nil, not an empty slice — that is the FAIL a tutorial hits first.
func (r *Regexp) FindStringSubmatch(s string) []string {
	return submatchStrings(s, r.FindSubmatchIndex([]byte(s)))
}

// FindSubmatchIndex returns the leftmost match's full span plus every group's span
// (2*(NumSubexp()+1) ints: start0,end0,start1,end1,...), or nil if there is no match — the
// same shape as regexp.Regexp.FindSubmatchIndex. A group that did not participate is -1,-1.
func (r *Regexp) FindSubmatchIndex(text []byte) []int {
	ctext, freeText := cBytes(text)
	defer freeText()
	spans := r.groupSlots()
	rc := C.real_match(r.re, (*C.char)(ctext), C.size_t(len(text)),
		0, C.size_t(len(text)), C.REAL_MODE_SEARCH, spanPtr(spans))
	if rc != 1 {
		return nil
	}
	return spansToIndices(spans)
}

// FindAllSubmatchIndex returns every non-overlapping match's full span plus every group's
// span, in order — the same shape as regexp.Regexp.FindAllSubmatchIndex(text, -1).
func (r *Regexp) FindAllSubmatchIndex(text []byte) [][]int {
	ctext, freeText := cBytes(text)
	defer freeText()
	it := C.real_find_iter(r.re, (*C.char)(ctext), C.size_t(len(text)))
	if it == nil {
		return nil
	}
	defer C.real_iter_free(it)
	spans := r.groupSlots()
	sp := spanPtr(spans)
	if sp == nil {
		return nil // closed / zero slots — never &spans[0] on empty
	}
	var out [][]int
	for {
		rc := C.real_iter_next(it, sp)
		if rc != 1 {
			break
		}
		out = append(out, spansToIndices(spans))
	}
	return out
}

// findAllSubmatchIndexN is FindAllSubmatchIndex with regexp's n: 0 → nil, <0 → all, >0 → at most n.
func (r *Regexp) findAllSubmatchIndexN(b []byte, n int) [][]int {
	if n == 0 {
		return nil
	}
	all := r.FindAllSubmatchIndex(b)
	if all == nil || n < 0 || n >= len(all) {
		return all
	}
	return all[:n]
}

// FindAllSubmatch returns successive matches and their groups, like regexp.Regexp.FindAllSubmatch.
// n is the cap (0 → nil, <0 → all).
func (r *Regexp) FindAllSubmatch(b []byte, n int) [][][]byte {
	all := r.findAllSubmatchIndexN(b, n)
	if all == nil {
		return nil
	}
	out := make([][][]byte, len(all))
	for i, idx := range all {
		out[i] = submatchBytes(b, idx)
	}
	return out
}

// FindAllStringSubmatch is FindAllSubmatch on a string, like regexp.Regexp.FindAllStringSubmatch.
func (r *Regexp) FindAllStringSubmatch(s string, n int) [][]string {
	all := r.findAllSubmatchIndexN([]byte(s), n)
	if all == nil {
		return nil
	}
	out := make([][]string, len(all))
	for i, idx := range all {
		out[i] = submatchStrings(s, idx)
	}
	return out
}

// FullMatch reports whether the ENTIRE text matches — a REAL extension over Go's standard
// regexp package, which has no fullmatch mode at all (only MatchString, which is really a
// search: `re.MatchString("x")` on pattern "ab" against "xaby" returns true). Exercises
// real_match's REAL_MODE_FULLMATCH.
func (r *Regexp) FullMatch(text []byte) bool {
	ctext, freeText := cBytes(text)
	defer freeText()
	spans := r.groupSlots()
	rc := C.real_match(r.re, (*C.char)(ctext), C.size_t(len(text)),
		0, C.size_t(len(text)), C.REAL_MODE_FULLMATCH, spanPtr(spans))
	return rc == 1
}

// dollarProbe is a dummy match so regexp.Expand itself classifies templates.
// A handwritten $1/$&/${name} list was wrong in both directions: it missed
// $name (Go's documented spelling) and rejected $& (which Expand leaves literal).
var (
	dollarProbe      = regexp.MustCompile(`a`)
	dollarProbeSrc   = []byte("a")
	dollarProbeMatch = []int{0, 1}
)

// regexpDollarTemplate reports whether repl uses regexp's Expand spelling
// ($name / ${name} / $1) rather than REAL/Python \1 / \g<name>. Those are left
// literal by the C ABI, so a regexp migrator would see a successful replace
// that did not substitute. We refuse instead of translating. $$ is an escaped
// dollar, not a variable — neutralized before asking Expand.
func regexpDollarTemplate(repl []byte) bool {
	neutral := bytes.ReplaceAll(repl, []byte("$$"), []byte{0, 0})
	out := dollarProbe.Expand(nil, neutral, dollarProbeSrc, dollarProbeMatch)
	return !bytes.Equal(out, neutral)
}

// ReplaceAll applies repl (a REAL/re-style template: \1, \g<name>, ...) to every
// non-overlapping match, mirroring regexp.Regexp.ReplaceAll's shape — though Go's stdlib uses
// $name-style templates where REAL uses \1-style. A `$1` / `$name` / `${name}` template is an
// error, not a silent no-op (see README.md). Two-call convention: size, then fill.
func (r *Regexp) ReplaceAll(text, repl []byte) ([]byte, error) {
	if regexpDollarTemplate(repl) {
		return nil, errors.New("ReplaceAll: $1 / $name / ${name} are regexp templates; this package uses \\1 / \\g<name>")
	}
	ctext, freeText := cBytes(text)
	defer freeText()
	crepl, freeRepl := cBytes(repl)
	defer freeRepl()
	var errbuf [256]C.char
	var nsubs C.size_t
	need := C.real_sub(r.re, (*C.char)(ctext), C.size_t(len(text)),
		(*C.char)(crepl), C.size_t(len(repl)), 0,
		nil, 0, &nsubs, &errbuf[0], C.size_t(len(errbuf)))
	if need == sizeMax {
		return nil, errors.New(C.GoString(&errbuf[0]))
	}
	out := make([]byte, int(need))
	if need > 0 {
		C.real_sub(r.re, (*C.char)(ctext), C.size_t(len(text)),
			(*C.char)(crepl), C.size_t(len(repl)), 0,
			(*C.char)(unsafe.Pointer(&out[0])), C.size_t(len(out)), &nsubs,
			&errbuf[0], C.size_t(len(errbuf)))
	}
	return out, nil
}
