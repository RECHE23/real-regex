// Package real_test — the landing page's Go quickstart tab (docs/site/index.md). `go test`
// already compiles and RUNS every `Example…` function that carries a `// Output:` comment (no
// extra wiring needed — see the go job in ci.yml / `make go-test`), so the code the page shows
// is exactly the code that's tested, never merely illustrative (doc-site P1b-A gate-snippet).
//
// Go requires every import to precede all other top-level declarations (no mid-function
// `import`, unlike C's textual #include), so the landing's 4-line snippet — an import line, a
// blank line, then two function-body statements — cannot live in one contiguous block the way
// examples/cpp/quickstart.cpp's redundant #include trick allows for C++. docs/site/index.md's Go
// tab therefore selects this file's exact LINE NUMBERS (a Sphinx literalinclude `:lines:`
// selector) instead of a single start-after/end-before marker pair. The `[quickstart-import]`
// and `[quickstart-body]` comment markers below are for human readers of this file only; if you
// resequence any line between them (including this header comment's own line count), update
// docs/site/index.md's `:lines:` value to match, and reconfirm with `make docs-site` (a
// selector pointing at the wrong lines still builds — it does not path-fail like a stale
// literalinclude target, so this is the one place a silent drift is possible; `go test` proves
// the CODE stays correct, but only a manual page diff catches a stale line selector).
package real_test

import "fmt"

// [quickstart-import]
import real "github.com/RECHE23/real-regex/bindings/go"

// [/quickstart-import]

// ExampleMustCompile is the landing page's Go quickstart: compile a pattern (drop-in for
// regexp.MustCompile), search bytes, and get the capture-group byte offsets back.
func ExampleMustCompile() {
	// Flush-left (not tab-indented like the rest of this function): the marked region below is
	// reproduced byte-for-byte by docs/site/index.md's Go tab (see the package doc comment above)
	// — Go does not care about statement indentation, so this stays copy-pasteable as-is.
	// [quickstart-body]
re := real.MustCompile(`(\w+)@(\w+)`)              // drop-in for regexp
re.FindSubmatchIndex([]byte("info@example.com"))  // capture-group offsets
	// [/quickstart-body]

	fmt.Println(re.FindSubmatchIndex([]byte("info@example.com")))
	// Output:
	// [0 12 0 4 5 12]
}
