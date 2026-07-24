// Package real provides Go bindings to the REAL regex engine (linear-time, ReDoS-safe) via
// cgo, over the C ABI at bindings/c/real_capi.h (v2026.7.39+, frozen-additive — vendored here,
// see vendor_include/ and the Makefile's `vendor`/`check-vendor` targets).
//
// v0.1: a regexp-idiomatic subset (Compile/MustCompile/FindAllIndex/FindSubmatchIndex/
// ReplaceAll/RegexSet) plus REAL extensions regexp has no equivalent for at all (FullMatch,
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
	"errors"
	"runtime"
	"unsafe"
)

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
type Regexp struct {
	re *C.real_regex
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
	r := &Regexp{re: h}
	runtime.SetFinalizer(r, (*Regexp).Close)
	return r, nil
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
// zero-values (NumSubexp → 0; SubexpNames → empty slice; FindAll*/FindSubmatchIndex → nil;
// FullMatch → false; ReplaceAll → error). Prefer not to use a closed Regexp — the guarantee is
// crash-freedom and stable sentinels, not a second valid lifetime.
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

// ReplaceAll applies repl (a REAL/re-style template: \1, \g<name>, ...) to every
// non-overlapping match, mirroring regexp.Regexp.ReplaceAll's shape — though Go's stdlib uses
// $1-style templates where REAL uses \1-style; a documented flavor divergence, not silently
// translated here (see README.md). Two-call convention: size, then fill.
func (r *Regexp) ReplaceAll(text, repl []byte) ([]byte, error) {
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
