package real

import (
	"errors"
	"strings"
	"testing"
)

// CompileWith holds a pattern to Options lower than the build's limits: a pattern past one fails
// with an error wrapping ErrLimit, in Compile's message format; zero fields keep the build's.
func TestCompileWithHoldsEachLimit(t *testing.T) {
	cases := []struct {
		opts         Options
		within, past string
	}{
		{Options{MaxRepeatCount: 5}, "a{5}", "a{6}"},
		{Options{MaxGroupCount: 2}, "(a)(b)", "(a)(b)(c)"},
		{Options{MaxNestingDepth: 3}, "(?:(?:(?:a)))", "(?:(?:(?:(?:a))))"},
		{Options{MaxLookaroundLength: 4}, "(?=abcd)a", "(?=abcde)a"},
		{Options{MaxProgramSize: 100}, strings.Repeat("x", 20), strings.Repeat("x", 150)},
	}
	for _, c := range cases {
		re, err := CompileWith(c.within, c.opts)
		if err != nil {
			t.Fatalf("%+v: %q refused: %v", c.opts, c.within, err)
		}
		re.Close()
		if _, err := CompileWith(c.past, c.opts); !errors.Is(err, ErrLimit) {
			t.Fatalf("%+v: %q: want ErrLimit, got %v", c.opts, c.past, err)
		}
	}
	_, err := CompileWith("a{6}", Options{MaxRepeatCount: 5})
	if err.Error() != "regex_error at 2: repetition count too large" {
		t.Fatalf("message: %q", err.Error())
	}
}

func TestCompileWithDefaultsAndCeilings(t *testing.T) {
	re, err := CompileWith(`(\w+)@(\w+)`, Options{})
	if err != nil || re.FindStringSubmatch("mail me@host now")[2] != "host" {
		t.Fatalf("defaults: %v", err)
	}
	re.Close()
	if _, err := CompileWith("a", Options{MaxRepeatCount: 5000}); !errors.Is(err, ErrLimit) {
		t.Fatalf("past the ceiling: %v", err)
	}
	if _, err := CompileWith("a", Options{MaxGroupCount: 1 << 40}); !errors.Is(err, ErrLimit) {
		t.Fatalf("past an int32: %v", err)
	}
	if _, err := CompileWith("a", Options{MaxDFAMemory: -1}); err == nil || errors.Is(err, ErrLimit) {
		t.Fatalf("negative: %v", err)
	}
	if _, err := CompileWith("a)", Options{}); err == nil || errors.Is(err, ErrLimit) {
		t.Fatalf("syntax: %v", err)
	}
	if _, err := CompileWith("a{2000}", Options{}); !errors.Is(err, ErrLimit) {
		t.Fatalf("the build's ceiling through CompileWith: %v", err)
	}
	if _, err := Compile("a{2000}"); err == nil || errors.Is(err, ErrLimit) { // Compile's contract is unchanged
		t.Fatalf("Compile: %v", err)
	}
}

// The DFA's memory bounds its cache, not the answer.
func TestCompileWithDFAMemoryKeepsTheAnswer(t *testing.T) {
	text := strings.Repeat("x ", 5000) + "gamma7"
	for _, memory := range []int{1, 4096, 1 << 30} {
		re, err := CompileWith(`\b(?:alpha|beta|gamma)\w*\d`, Options{MaxDFAMemory: memory})
		if err != nil {
			t.Fatal(err)
		}
		if got := re.FindString(text); got != "gamma7" {
			t.Fatalf("memory %d: %q", memory, got)
		}
		re.Close()
	}
}
