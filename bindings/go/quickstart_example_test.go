// Package real_test — the landing page's Go quickstart tab. `go test` already
// compiles and RUNS every `Example…` function
// that carries a `// Output:` comment (no extra wiring needed — see the go job in ci.yml /
// `make go-test`), so the code the page shows is exactly the code that's tested, never
// merely illustrative.
//
// Go requires every import to precede all other top-level declarations (no mid-function
// `import`, unlike C's textual #include), so the landing's 4-line snippet — an import line, a
// blank line, then two function-body statements — cannot live in one contiguous block the way
// examples/cpp/quickstart.cpp's redundant #include trick allows for C++. The landing's Go tab
// is therefore built from TWO marker pairs, `[quickstart-import]` and `[quickstart-body]`
// below, concatenated: conf.py's `_highlight_go_quickstart` reads both regions
// directly and joins them with a blank line, the same
// start-after/end-before contract the other 3 languages' single marker pair uses. Resequencing
// lines between the markers is safe by construction — unlike a fixed `:lines:`
// Sphinx literalinclude selector, which points at fixed line numbers and can
// go silently stale (it still built successfully even after a resequence meant the wrong lines
// rendered — `go test` proved the CODE stayed correct, but only a manual page diff would have
// caught a stale line selector).
package real_test

import "fmt"

// [quickstart-import]
import real "github.com/RECHE23/real-regex/bindings/go"

// [/quickstart-import]

// ExampleMustCompile is the landing page's Go quickstart: compile a pattern and
// MatchString, the first method a regexp tutorial writes.
func ExampleMustCompile() {
	// Flush-left (not tab-indented like the rest of this function): the marked region below is
	// reproduced byte-for-byte by the landing page's Go tab (see the package doc comment above)
	// — Go does not care about statement indentation, so this stays copy-pasteable as-is.
	// [quickstart-body]
	re := real.MustCompile(`\d+`)
	fmt.Print(re.MatchString("x42"))
	// [/quickstart-body]

	// Output:
	// true
}
