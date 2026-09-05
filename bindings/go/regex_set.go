package real

/*
#include "real_capi.h"
*/
import "C"

import (
	"errors"
	"runtime"
)

// RegexSet is a multi-pattern which-matched set (real::regex_set — Stage-1 N-walks, or a
// fused Stage-2 single-pass DFA once enough members are DFA-eligible; see regex_set.hpp).
// Go's own regexp package has no equivalent — there is nothing to map this onto beyond REAL's
// own C++/Python bindings, which have the identical shape. A value copy of RegexSet still
// shares the C pointer — unlike regexp.Regexp, *s is not a safe clone. noCopy makes
// that copy a go vet -copylocks diagnostic; the compiler will still accept it.
type RegexSet struct {
	_   noCopy
	set *C.real_regex_set
}

// CompileSet compiles patterns into a set. Fails (with an error, no partial/silent skip) if
// any pattern is invalid. Bitset order in IsMatch/Matches is construction order.
func CompileSet(patterns []string) (*RegexSet, error) {
	cpatterns := make([]*C.char, len(patterns))
	clens := make([]C.size_t, len(patterns))
	frees := make([]func(), 0, len(patterns))
	defer func() {
		for _, f := range frees {
			f()
		}
	}()
	for i, p := range patterns {
		ptr, free := cBytes([]byte(p))
		cpatterns[i] = (*C.char)(ptr)
		clens[i] = C.size_t(len(p))
		frees = append(frees, free)
	}
	var patternsPtr **C.char
	var lensPtr *C.size_t
	if len(patterns) > 0 {
		patternsPtr = &cpatterns[0]
		lensPtr = &clens[0]
	}
	var errbuf [256]C.char
	var code C.int
	// dollarEndOnly for the same reason Compile carries it: `$` is regexp's end-of-text here, not
	// re's end-or-before-a-trailing-newline. A set whose members answered a different `$` from a
	// singly-compiled pattern would be the worse bug of the two.
	h := C.real_set_compile(patternsPtr, lensPtr, C.size_t(len(patterns)), dollarEndOnly,
		&errbuf[0], C.size_t(len(errbuf)), &code)
	if h == nil {
		return nil, errors.New(C.GoString(&errbuf[0]))
	}
	s := &RegexSet{set: h}
	runtime.SetFinalizer(s, (*RegexSet).Close)
	return s, nil
}

// Close releases the compiled set. Idempotent.
func (s *RegexSet) Close() error {
	if s.set != nil {
		C.real_set_free(s.set)
		s.set = nil
		runtime.SetFinalizer(s, nil)
	}
	return nil
}

// Size returns the number of patterns in the set.
func (s *RegexSet) Size() int {
	return int(C.real_set_size(s.set))
}

// IsMatch reports whether any pattern matches text at least once (stops at the first hit).
func (s *RegexSet) IsMatch(text []byte) bool {
	ctext, freeText := cBytes(text)
	defer freeText()
	return C.real_set_is_match(s.set, (*C.char)(ctext), C.size_t(len(text))) == 1
}

// Matches returns which patterns match text at least once, in construction order.
func (s *RegexSet) Matches(text []byte) []bool {
	ctext, freeText := cBytes(text)
	defer freeText()
	n := s.Size()
	hits := make([]C.uint8_t, n)
	var hitsPtr *C.uint8_t
	if n > 0 {
		hitsPtr = &hits[0]
	}
	rc := C.real_set_matches(s.set, (*C.char)(ctext), C.size_t(len(text)), hitsPtr)
	out := make([]bool, n)
	if rc == 0 {
		for i, h := range hits {
			out[i] = h != 0
		}
	}
	return out
}
