package real

import (
	"reflect"
	"testing"
)

func TestRegexSetBasic(t *testing.T) {
	s, err := CompileSet([]string{"alpha", "beta", "gamma"})
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	defer s.Close()
	if got := s.Size(); got != 3 {
		t.Fatalf("got %d, want 3", got)
	}
	got := s.Matches([]byte("xx beta yy"))
	want := []bool{false, true, false}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
	if !s.IsMatch([]byte("please find beta")) {
		t.Fatal("expected a match")
	}
	if s.IsMatch([]byte("nothing")) {
		t.Fatal("expected no match")
	}
}

func TestRegexSetInvalidPatternErrors(t *testing.T) {
	_, err := CompileSet([]string{"ok", "("})
	if err == nil {
		t.Fatal("expected an error — no silent skip of a bad pattern")
	}
}

func TestRegexSetEmpty(t *testing.T) {
	s, err := CompileSet(nil)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	defer s.Close()
	if got := s.Size(); got != 0 {
		t.Fatalf("got %d, want 0", got)
	}
	if s.IsMatch([]byte("anything")) {
		t.Fatal("an empty set must never match")
	}
	if got := s.Matches([]byte("anything")); len(got) != 0 {
		t.Fatalf("got %v, want an empty slice", got)
	}
}

func TestRegexSetCloseIsIdempotent(t *testing.T) {
	s := mustCompileSet(t, []string{"x"})
	s.Close()
	s.Close()
}

func mustCompileSet(t *testing.T, patterns []string) *RegexSet {
	t.Helper()
	s, err := CompileSet(patterns)
	if err != nil {
		t.Fatalf("CompileSet(%v): %v", patterns, err)
	}
	return s
}
