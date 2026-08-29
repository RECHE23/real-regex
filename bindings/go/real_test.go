package real

import (
	"fmt"
	"reflect"
	"regexp"
	"testing"
)

// --- EngineVersion (the vendored engine's own CalVer, decoupled from this module's semver) --

func TestEngineVersion(t *testing.T) {
	// A shape check ("YYYY.M.PATCH"), not a hardcoded version string -- this test must not need
	// editing on every engine bump. Non-empty is the load-bearing assertion: an empty string
	// would mean the vendored real/version.hpp's macros silently stopped stringizing right.
	matched, err := regexp.MatchString(`^\d{4}\.\d{1,2}\.\d+$`, EngineVersion)
	if err != nil {
		t.Fatalf("regexp error: %v", err)
	}
	if !matched {
		t.Fatalf("EngineVersion %q does not look like a CalVer string", EngineVersion)
	}
}

// --- Compile / MustCompile plumbing ---------------------------------------------------------

func TestCompileValid(t *testing.T) {
	r, err := Compile(`\d+`)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	defer r.Close()
}

func TestCompileInvalidReturnsError(t *testing.T) {
	_, err := Compile(`(`)
	if err == nil {
		t.Fatal("expected an error for an unbalanced paren")
	}
}

func TestMustCompilePanicsOnError(t *testing.T) {
	defer func() {
		if recover() == nil {
			t.Fatal("expected MustCompile to panic on an invalid pattern")
		}
	}()
	MustCompile(`(`)
}

// QuoteMeta is regexp.QuoteMeta; the load-bearing pin is not that equality
// (a tautology) but that REAL's grammar accepts the escaped form as a literal.
// A new REAL metacharacter outside \.+*?()|[]{}^$ would fail Compile or
// FullMatch-self here. (?x)+QuoteMeta is out of contract: this binding
// compiles with no flags, QuoteMeta is the whole pattern, and regexp has no (?x).
func TestQuoteMetaCompilesAndFullMatchesEveryASCIIByte(t *testing.T) {
	for c := 0; c < 128; c++ {
		s := string([]byte{byte(c)})
		q := QuoteMeta(s)
		re, err := Compile(q)
		if err != nil {
			t.Fatalf("U+%02X QuoteMeta=%q: compile: %v", c, q, err)
		}
		if !re.FullMatch([]byte(s)) {
			re.Close()
			t.Fatalf("U+%02X QuoteMeta=%q: FullMatch self failed", c, q)
		}
		// `.` FullMatch `.` succeeds quoted or not; a leftover wildcard would
		// also FullMatch "x". Skip when the byte under test is itself 'x'.
		if c != 'x' && re.FullMatch([]byte("x")) {
			re.Close()
			t.Fatalf("U+%02X QuoteMeta=%q: FullMatch \"x\" (not a literal)", c, q)
		}
		re.Close()
	}
}

func TestPackageMatchString(t *testing.T) {
	ok, err := MatchString(`\d+`, "x42y")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !ok {
		t.Fatal("MatchString(`\\d+`, \"x42y\"): want true (a search, not FullMatch)")
	}
	ok, err = MatchString(`\d+`, "abc")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if ok {
		t.Fatal("MatchString(`\\d+`, \"abc\"): want false")
	}
	if _, err := MatchString(`(`, "a"); err == nil {
		t.Fatal("expected an error for an unbalanced paren")
	}
}

func TestPackageMatchMatchesStdlib(t *testing.T) {
	cases := []struct{ pat, s string }{
		{`abc`, `xabcy`},
		{`\d+`, `a1 b22`},
		{`zzz`, `abc`},
	}
	for _, c := range cases {
		got, err := MatchString(c.pat, c.s)
		if err != nil {
			t.Fatalf("%q: %v", c.pat, err)
		}
		want, err := regexp.MatchString(c.pat, c.s)
		if err != nil {
			t.Fatalf("regexp %q: %v", c.pat, err)
		}
		if got != want {
			t.Fatalf("%q on %q: MatchString got %v want %v", c.pat, c.s, got, want)
		}
		gotB, err := Match(c.pat, []byte(c.s))
		if err != nil {
			t.Fatalf("%q bytes: %v", c.pat, err)
		}
		if gotB != got {
			t.Fatalf("%q on %q: Match vs MatchString %v vs %v", c.pat, c.s, gotB, got)
		}
	}
}

// String is regexp.Regexp.String: the source text, not a default dump of the
// C pointer. Patterns the stdlib also compiles are compared against it;
// patterns it rejects still have to round-trip our own source.
func TestStringMatchesStdlib(t *testing.T) {
	shared := []string{`abc`, `\d+`, ``, `(?P<g>\w+)@(\w+)`, `[a-z]+`}
	for _, p := range shared {
		r := MustCompile(p)
		std := regexp.MustCompile(p)
		if got, want := r.String(), std.String(); got != want {
			r.Close()
			t.Fatalf("%q: String() %q, regexp.Regexp.String() %q", p, got, want)
		}
		if got, want := fmt.Sprintf("%s", r), fmt.Sprintf("%s", std); got != want {
			r.Close()
			t.Fatalf("%q: %%s %q, stdlib %%s %q", p, got, want)
		}
		r.Close()
	}
}

func TestStringKeepsSourceStdlibRejects(t *testing.T) {
	p := `foo(?=bar)`
	if _, err := regexp.Compile(p); err == nil {
		t.Fatal("expected Go's regexp to reject lookahead")
	}
	r := MustCompile(p)
	defer r.Close()
	if got := r.String(); got != p {
		t.Fatalf("String() %q, want %q", got, p)
	}
}

func TestCloseIsIdempotent(t *testing.T) {
	r := MustCompile(`x`)
	r.Close()
	r.Close() // must not double-free / crash
}

// Post-Close: every public method — by enumeration, not sample — must not crash and must
// return the documented zero-value sentinel.
func TestClosedHandleIsSafe(t *testing.T) {
	r := MustCompile(`(?P<g>\w+)@(\w+)`)
	r.Close()

	// Regexp surface (complete public set). String is not a handle query: the
	// source text lives on the Go value and survives Close.
	if got := r.String(); got != `(?P<g>\w+)@(\w+)` {
		t.Fatalf("String after Close: got %q, want the source text", got)
	}
	if n := r.NumSubexp(); n != 0 {
		t.Fatalf("NumSubexp after Close: got %d, want 0", n)
	}
	if names := r.SubexpNames(); len(names) != 0 {
		t.Fatalf("SubexpNames after Close: got len %d, want 0", len(names))
	}
	if got := r.FindAllIndex([]byte("a@b")); got != nil {
		t.Fatalf("FindAllIndex after Close: got %v, want nil", got)
	}
	if got := r.FindSubmatchIndex([]byte("a@b")); got != nil {
		t.Fatalf("FindSubmatchIndex after Close: got %v, want nil", got)
	}
	if got := r.FindAllSubmatchIndex([]byte("a@b")); got != nil {
		t.Fatalf("FindAllSubmatchIndex after Close: got %v, want nil", got)
	}
	if got := r.FindSubmatch([]byte("a@b")); got != nil {
		t.Fatalf("FindSubmatch after Close: got %v, want nil", got)
	}
	if got := r.FindStringSubmatch("a@b"); got != nil {
		t.Fatalf("FindStringSubmatch after Close: got %v, want nil", got)
	}
	if got := r.FindAllSubmatch([]byte("a@b"), -1); got != nil {
		t.Fatalf("FindAllSubmatch after Close: got %v, want nil", got)
	}
	if got := r.FindAllStringSubmatch("a@b", -1); got != nil {
		t.Fatalf("FindAllStringSubmatch after Close: got %v, want nil", got)
	}
	if r.Match([]byte("a@b")) {
		t.Fatal("Match after Close: got true, want false")
	}
	if r.MatchString("a@b") {
		t.Fatal("MatchString after Close: got true, want false")
	}
	if got := r.Find([]byte("a@b")); got != nil {
		t.Fatalf("Find after Close: got %q, want nil", got)
	}
	if got := r.FindString("a@b"); got != "" {
		t.Fatalf("FindString after Close: got %q, want empty", got)
	}
	if got := r.FindIndex([]byte("a@b")); got != nil {
		t.Fatalf("FindIndex after Close: got %v, want nil", got)
	}
	if got := r.FindAll([]byte("a@b"), -1); got != nil {
		t.Fatalf("FindAll after Close: got %v, want nil", got)
	}
	if got := r.FindAllString("a@b", -1); got != nil {
		t.Fatalf("FindAllString after Close: got %v, want nil", got)
	}
	if got := r.Split("a@b", -1); !reflect.DeepEqual(got, []string{"a@b"}) {
		t.Fatalf("Split after Close: got %v, want the no-match whole string", got)
	}
	if r.FullMatch([]byte("a@b")) {
		t.Fatal("FullMatch after Close: got true, want false")
	}
	if _, err := r.ReplaceAll([]byte("a@b"), []byte("x")); err == nil {
		t.Fatal("ReplaceAll after Close: expected error, got nil")
	}

	// RegexSet surface (complete public set)
	s, err := CompileSet([]string{`a`, `b`})
	if err != nil {
		t.Fatalf("CompileSet: %v", err)
	}
	s.Close()
	if n := s.Size(); n != 0 {
		t.Fatalf("RegexSet.Size after Close: got %d, want 0", n)
	}
	if s.IsMatch([]byte("a")) {
		t.Fatal("RegexSet.IsMatch after Close: got true, want false")
	}
	if m := s.Matches([]byte("a")); len(m) != 0 {
		t.Fatalf("RegexSet.Matches after Close: got %v, want empty", m)
	}
}

// Long group names must round-trip bit-for-bit (no fixed-buffer OOB / truncation).
func TestLongGroupName(t *testing.T) {
	// 300 'a' characters — past the old fixed [256] buffer.
	long := make([]byte, 300)
	for i := range long {
		long[i] = 'a'
	}
	pat := "(?P<" + string(long) + ">x)"
	r, err := Compile(pat)
	if err != nil {
		t.Fatalf("Compile long name: %v", err)
	}
	defer r.Close()
	names := r.SubexpNames()
	if len(names) < 2 {
		t.Fatalf("SubexpNames len %d, want >= 2", len(names))
	}
	if names[1] != string(long) {
		t.Fatalf("group name mismatch: got len %d want %d", len(names[1]), len(long))
	}
}

// --- differential vs Go's stdlib regexp (shared RE2-subset syntax) -------------------------

func diffFindAllIndex(t *testing.T, pattern, text string) {
	t.Helper()
	r := MustCompile(pattern)
	defer r.Close()
	got := r.FindAllIndex([]byte(text))

	std := regexp.MustCompile(pattern)
	want := std.FindAllIndex([]byte(text), -1)

	if len(got) != len(want) {
		t.Fatalf("%q on %q: got %d matches, regexp stdlib got %d (got=%v want=%v)",
			pattern, text, len(got), len(want), got, want)
	}
	for i := range want {
		if !reflect.DeepEqual(got[i], want[i]) {
			t.Fatalf("%q on %q: match %d got %v, want %v", pattern, text, i, got[i], want[i])
		}
	}
}

func TestMatchFindSplitMatchStdlib(t *testing.T) {
	cases := []struct{ pat, s string }{
		{`abc`, `xabcyabcz`},
		{`\d+`, `a1 b22 c`},
		{`zzz`, `abc`},
		{``, `x`},
		{`a*`, `bb`},
		{`([a-z]+)@([a-z]+)`, `xx a@b yy`},
		{`(a)|(b)`, `b`},
	}
	for _, c := range cases {
		r := MustCompile(c.pat)
		std := regexp.MustCompile(c.pat)
		if got, want := r.MatchString(c.s), std.MatchString(c.s); got != want {
			r.Close()
			t.Fatalf("%q on %q: MatchString got %v want %v", c.pat, c.s, got, want)
		}
		if got, want := r.FindString(c.s), std.FindString(c.s); got != want {
			r.Close()
			t.Fatalf("%q on %q: FindString %q want %q", c.pat, c.s, got, want)
		}
		if got, want := r.FindIndex([]byte(c.s)), std.FindIndex([]byte(c.s)); !reflect.DeepEqual(got, want) {
			r.Close()
			t.Fatalf("%q on %q: FindIndex got %v want %v", c.pat, c.s, got, want)
		}
		if got, want := r.FindStringSubmatch(c.s), std.FindStringSubmatch(c.s); !reflect.DeepEqual(got, want) {
			r.Close()
			t.Fatalf("%q on %q: FindStringSubmatch got %q want %q", c.pat, c.s, got, want)
		}
		if got, want := r.FindSubmatch([]byte(c.s)), std.FindSubmatch([]byte(c.s)); !reflect.DeepEqual(got, want) {
			r.Close()
			t.Fatalf("%q on %q: FindSubmatch got %q want %q", c.pat, c.s, got, want)
		}
		for _, n := range []int{-1, 0, 1, 2, 99} {
			if got, want := r.FindAllString(c.s, n), std.FindAllString(c.s, n); !reflect.DeepEqual(got, want) {
				r.Close()
				t.Fatalf("%q on %q n=%d: FindAllString got %v want %v", c.pat, c.s, n, got, want)
			}
			if got, want := r.FindAllStringSubmatch(c.s, n), std.FindAllStringSubmatch(c.s, n); !reflect.DeepEqual(got, want) {
				r.Close()
				t.Fatalf("%q on %q n=%d: FindAllStringSubmatch got %q want %q", c.pat, c.s, n, got, want)
			}
			if got, want := r.FindAllStringSubmatchIndex(c.s, n), std.FindAllStringSubmatchIndex(c.s, n); !reflect.DeepEqual(got, want) {
				r.Close()
				t.Fatalf("%q on %q n=%d: FindAllStringSubmatchIndex got %v want %v", c.pat, c.s, n, got, want)
			}
			if got, want := r.FindAllSubmatch([]byte(c.s), n), std.FindAllSubmatch([]byte(c.s), n); !reflect.DeepEqual(got, want) {
				r.Close()
				t.Fatalf("%q on %q n=%d: FindAllSubmatch got %q want %q", c.pat, c.s, n, got, want)
			}
			if got, want := r.Split(c.s, n), std.Split(c.s, n); !reflect.DeepEqual(got, want) {
				r.Close()
				t.Fatalf("%q on %q n=%d: Split got %v want %v", c.pat, c.s, n, got, want)
			}
		}
		r.Close()
	}
}

func TestDifferential_Literal(t *testing.T) {
	diffFindAllIndex(t, `abc`, `xabcyabcz`)
}

func TestDifferential_CharClassQuantifier(t *testing.T) {
	diffFindAllIndex(t, `[a-z]+`, `abc123def456ghi`)
}

func TestDifferential_BoundedRepeat(t *testing.T) {
	diffFindAllIndex(t, `\d{2,4}`, `1 22 333 4444 55555`)
}

func TestDifferential_Alternation(t *testing.T) {
	diffFindAllIndex(t, `cat|dog`, `cats and dogs and cows and catfish`)
}

func TestDifferential_OptionalChar(t *testing.T) {
	diffFindAllIndex(t, `colou?r`, `color and colour and colouur`)
}

// --- REAL extensions beyond RE2 (Go's regexp cannot even compile these) --------------------

func TestBeyondRE2_BoundedLookahead(t *testing.T) {
	if _, err := regexp.Compile(`foo(?=bar)`); err == nil {
		t.Fatal("expected Go's regexp to reject lookahead (sanity check on the divergence claim)")
	}
	r := MustCompile(`foo(?=bar)`)
	defer r.Close()
	got := r.FindAllIndex([]byte(`foobar foobaz foobar`))
	want := [][]int{{0, 3}, {14, 17}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestBeyondRE2_Possessive(t *testing.T) {
	if _, err := regexp.Compile(`a++b`); err == nil {
		t.Fatal("expected Go's regexp to reject a possessive quantifier")
	}
	r := MustCompile(`a++b`)
	defer r.Close()
	if !r.FullMatch([]byte(`aaab`)) {
		t.Fatal(`"aaab" should fullmatch a++b`)
	}
	r2 := MustCompile(`^a++$`)
	defer r2.Close()
	if !r2.FullMatch([]byte(`aaa`)) {
		t.Fatal(`"aaa" should fullmatch ^a++$`)
	}
}

// --- FullMatch (a REAL extension) -----------------------------------------------------------

func TestFullMatch(t *testing.T) {
	r := MustCompile(`[a-z]+`)
	defer r.Close()
	if !r.FullMatch([]byte(`abc`)) {
		t.Fatal(`"abc" should fullmatch [a-z]+`)
	}
	if r.FullMatch([]byte(`abc123`)) {
		t.Fatal(`"abc123" should NOT fullmatch [a-z]+`)
	}
}

func TestFullMatchEmptyPattern(t *testing.T) {
	r := MustCompile(``)
	defer r.Close()
	if !r.FullMatch([]byte(``)) {
		t.Fatal(`empty pattern should fullmatch empty text`)
	}
	if r.FullMatch([]byte(`x`)) {
		t.Fatal(`empty pattern should NOT fullmatch non-empty text`)
	}
}

// --- ReplaceAll (via real_sub, two-call convention) -----------------------------------------

func TestReplaceAllLiteral(t *testing.T) {
	r := MustCompile(`\d+`)
	defer r.Close()
	got, err := r.ReplaceAll([]byte(`a1 b22 c333`), []byte(`#`))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if want := `a# b# c#`; string(got) != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestReplaceAllGroupSwap(t *testing.T) {
	r := MustCompile(`(\w+)@(\w+)`)
	defer r.Close()
	got, err := r.ReplaceAll([]byte(`a@b and cd@ef`), []byte(`\2@\1`))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if want := `b@a and ef@cd`; string(got) != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestReplaceAllNoMatch(t *testing.T) {
	r := MustCompile(`xyz`)
	defer r.Close()
	got, err := r.ReplaceAll([]byte(`abc`), []byte(`#`))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if string(got) != `abc` {
		t.Fatalf("got %q, want unchanged %q", got, `abc`)
	}
}

func TestReplaceAllBadTemplateErrors(t *testing.T) {
	r := MustCompile(`(a)`)
	defer r.Close()
	if _, err := r.ReplaceAll([]byte(`a`), []byte(`\9`)); err == nil {
		t.Fatal("expected an error for an out-of-range group reference")
	}
}

func TestRegexpDollarTemplate(t *testing.T) {
	// Pinned against regexp.Expand (Go 1.26): $name / ${name} / $1 are variables;
	// $& is not (ECMAScript, not extract()'s letter/digit/_). $$ is an escaped dollar.
	yes := []string{`$1`, `$2`, `$0`, `${name}`, `$year/$month`, `$x`, `x$1`, `$_`}
	no := []string{``, `$`, `$$`, `$$1`, `price $`, `#`, `\1`, `\2@\1`, `$&`, `$ x`, `end$`}
	for _, s := range yes {
		if !regexpDollarTemplate([]byte(s)) {
			t.Fatalf("%q: want regexp dollar template", s)
		}
	}
	for _, s := range no {
		if regexpDollarTemplate([]byte(s)) {
			t.Fatalf("%q: not a regexp $name / ${name} template", s)
		}
	}
}

func TestReplaceAllRegexpDollarTemplateErrors(t *testing.T) {
	r := MustCompile(`(?P<year>\d{4})-(?P<month>\d{2})`)
	defer r.Close()
	// The FAIL: $year/$month is the spelling Go documents and Expand substitutes.
	for _, repl := range []string{`$2`, `${month}`, `$year/$month`, `$x`} {
		got, err := r.ReplaceAll([]byte(`2026-08`), []byte(repl))
		if err == nil {
			t.Fatalf("ReplaceAll(%q): expected error, got %q", repl, got)
		}
	}
	// $& is not a regexp name — Expand leaves it literal; so do we.
	amp, err := r.ReplaceAll([]byte(`2026-08`), []byte(`$&`))
	if err != nil {
		t.Fatalf("$& is not a regexp template: unexpected error: %v", err)
	}
	if string(amp) != `$&` {
		t.Fatalf("$&: got %q, want literal $&", amp)
	}
	// $$ is regexp's escape for a literal dollar (Expand collapses it to one).
	// We leave both dollars: collapsing would be translating a $ spelling.
	for _, repl := range []string{`$$`, `$$1`} {
		got, err := r.ReplaceAll([]byte(`2026-08`), []byte(repl))
		if err != nil {
			t.Fatalf("ReplaceAll(%q): unexpected error: %v", repl, err)
		}
		if string(got) != repl {
			t.Fatalf("ReplaceAll(%q): got %q, want the dollars left verbatim", repl, got)
		}
	}
	// Backslash templates still substitute — same witness as TestReplaceAllGroupSwap.
	r2 := MustCompile(`(\w+)@(\w+)`)
	defer r2.Close()
	got, err := r2.ReplaceAll([]byte(`a@b`), []byte(`\2@\1`))
	if err != nil {
		t.Fatalf("backslash template: unexpected error: %v", err)
	}
	if want := `b@a`; string(got) != want {
		t.Fatalf("backslash template: got %q, want %q", got, want)
	}
}

// --- byte-oriented offsets, empty inputs, flavor divergence ---------------------------------

func TestFindAllIndexOnEmptyText(t *testing.T) {
	r := MustCompile(`x`)
	defer r.Close()
	if got := r.FindAllIndex([]byte(``)); got != nil {
		t.Fatalf("expected no matches on empty text, got %v", got)
	}
}

func TestFindAllIndexNoMatch(t *testing.T) {
	r := MustCompile(`xyz`)
	defer r.Close()
	if got := r.FindAllIndex([]byte(`abc`)); got != nil {
		t.Fatalf("expected no matches, got %v", got)
	}
}

func TestFindAllIndexUTF8ByteOffsets(t *testing.T) {
	// [a-z] (an explicit ASCII class) is unambiguous in both flavors, unlike \w (see
	// TestFlavorDivergence_WordShorthandIsUnicodeByDefault below).
	diffFindAllIndex(t, `[a-z]+`, "café world")
}

func TestFlavorDivergence_WordShorthandIsUnicodeByDefault(t *testing.T) {
	// \w is NOT a valid differential pattern against Go's stdlib regexp: REAL follows Python
	// re (Unicode-aware \w by default in text mode; matches "é"), RE2/Go's regexp is
	// ASCII-only by default (does not). Documented divergence, not a bug. README leads with
	// this — the single most likely surprise for a regexp migrator.
	r := MustCompile(`\w+`)
	defer r.Close()
	got := r.FindAllIndex([]byte("café world"))
	want := [][]int{{0, 5}, {6, 11}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}

	std := regexp.MustCompile(`\w+`)
	stdGot := std.FindAllIndex([]byte("café world"), -1)
	stdWant := [][]int{{0, 3}, {6, 11}}
	if !reflect.DeepEqual(stdGot, stdWant) {
		t.Fatalf("Go stdlib regexp's own ASCII-\\w behavior changed underneath this test: got %v, want %v",
			stdGot, stdWant)
	}
}

func TestConcurrentSharedPattern(t *testing.T) {
	r := MustCompile(`[a-z]+`)
	defer r.Close()
	done := make(chan bool, 8)
	for i := 0; i < 8; i++ {
		go func() {
			for j := 0; j < 100; j++ {
				r.FindAllIndex([]byte(`abc def ghi`))
			}
			done <- true
		}()
	}
	for i := 0; i < 8; i++ {
		<-done
	}
}

// --- submatch / named-group / unset matrix --------------------------------------

func TestFindSubmatchIndex(t *testing.T) {
	r := MustCompile(`(\w+)@(\w+)`)
	defer r.Close()
	got := r.FindSubmatchIndex([]byte(`a@b`))
	want := []int{0, 3, 0, 1, 2, 3}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestFindSubmatchIndex_NoMatch(t *testing.T) {
	r := MustCompile(`xyz`)
	defer r.Close()
	if got := r.FindSubmatchIndex([]byte(`abc`)); got != nil {
		t.Fatalf("expected nil, got %v", got)
	}
}

func TestFindSubmatchIndex_UnsetGroup(t *testing.T) {
	// "(a)|(b)" on "b": group 1 (the "a" alternative) never participates -- must be -1,-1,
	// matching regexp's own convention, not the C ABI's raw SIZE_MAX sentinel.
	r := MustCompile(`(a)|(b)`)
	defer r.Close()
	got := r.FindSubmatchIndex([]byte(`b`))
	want := []int{0, 1, -1, -1, 0, 1}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestFindAllSubmatchIndex(t *testing.T) {
	r := MustCompile(`(\w+)@(\w+)`)
	defer r.Close()
	got := r.FindAllSubmatchIndex([]byte(`a@b and cd@ef`))
	want := [][]int{
		{0, 3, 0, 1, 2, 3},
		{8, 13, 8, 10, 11, 13},
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestFindStringSubmatch(t *testing.T) {
	// The FAIL: the groups tutorial writes FindStringSubmatch, not FindSubmatchIndex.
	r := MustCompile(`(\w+)@(\w+)`)
	defer r.Close()
	got := r.FindStringSubmatch(`a@b`)
	want := []string{`a@b`, `a`, `b`}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestFindStringSubmatch_NoMatchIsNil(t *testing.T) {
	r := MustCompile(`xyz`)
	defer r.Close()
	if got := r.FindStringSubmatch(`abc`); got != nil {
		t.Fatalf("no match must be nil, not %q", got)
	}
	if got := r.FindSubmatch([]byte(`abc`)); got != nil {
		t.Fatalf("no match must be nil, not %q", got)
	}
}

func TestFindSubmatch_UnsetGroupIsNil(t *testing.T) {
	r := MustCompile(`(a)|(b)`)
	defer r.Close()
	got := r.FindSubmatch([]byte(`b`))
	want := [][]byte{[]byte(`b`), nil, []byte(`b`)}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %q, want %q (unset group is nil, not empty)", got, want)
	}
	if got[1] != nil {
		t.Fatalf("group 1 must be nil, got %q", got[1])
	}
	sgot := r.FindStringSubmatch(`b`)
	swant := []string{`b`, ``, `b`}
	if !reflect.DeepEqual(sgot, swant) {
		t.Fatalf("string form got %q, want %q", sgot, swant)
	}
}

func TestFindAllStringSubmatch(t *testing.T) {
	r := MustCompile(`(\w+)@(\w+)`)
	defer r.Close()
	got := r.FindAllStringSubmatch(`a@b and cd@ef`, -1)
	want := [][]string{
		{`a@b`, `a`, `b`},
		{`cd@ef`, `cd`, `ef`},
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %q, want %q", got, want)
	}
	if got := r.FindAllStringSubmatch(`a@b and cd@ef`, 0); got != nil {
		t.Fatalf("n=0 must be nil, got %q", got)
	}
	one := r.FindAllStringSubmatch(`a@b and cd@ef`, 1)
	if !reflect.DeepEqual(one, want[:1]) {
		t.Fatalf("n=1 got %q, want %q", one, want[:1])
	}
}

func TestSubexpNames(t *testing.T) {
	r := MustCompile(`(?P<user>\w+)@(?P<host>\w+)`)
	defer r.Close()
	got := r.SubexpNames()
	want := []string{"", "user", "host"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestSubexpNames_Unnamed(t *testing.T) {
	r := MustCompile(`(\w+)@(\w+)`)
	defer r.Close()
	got := r.SubexpNames()
	want := []string{"", "", ""}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestNumSubexp(t *testing.T) {
	r := MustCompile(`(\w+)@(\w+)`)
	defer r.Close()
	if got := r.NumSubexp(); got != 2 {
		t.Fatalf("got %d, want 2", got)
	}
	r0 := MustCompile(`\w+`)
	defer r0.Close()
	if got := r0.NumSubexp(); got != 0 {
		t.Fatalf("got %d, want 0", got)
	}
}

// The empty-match rule is regexp's, not the engine's: regexp ignores an empty match abutting the
// preceding one, the engine (Python re's model) reports it. Every FindAll* and Split derive from
// one enumeration, so the whole family is compared against regexp rather than against transcribed
// expectations — the stdlib owns the answers. Without the filter `x*` over "axbxc" split to
// ["a" "" "b" "" "c"] where regexp gives ["a" "b" "c"].
func TestEmptyMatchEnumerationMatchesStdlib(t *testing.T) {
	cases := []struct{ pat, s string }{
		{`x*`, "axbxc"},   // empty matches abutting each "x"
		{`b*`, "abc"},     // and at the very end
		{`,`, "a,b,,c,"},  // non-empty separators: empty FIELDS must survive
		{`\s*`, "a b  c"}, // runs of varying length
		{`a*`, "aaa"},     // one match spanning everything
		{``, "abc"},       // the empty pattern: every position, none abutting
		{`x*`, ""},        // empty subject
		{`\b`, "ab cd"},   // zero-width assertions are never abutting
		{`(a)|(b)`, "ab"}, // groups: the submatch shape travels too
		{`a?`, "bab"},
		{`(ab|)`, "abab"}, // nullable alternation, the shape the engine fix declined
	}
	for _, c := range cases {
		std, ours := regexp.MustCompile(c.pat), MustCompile(c.pat)
		if got, want := ours.FindAllStringIndex(c.s, -1), std.FindAllStringIndex(c.s, -1); !reflect.DeepEqual(got, want) {
			t.Errorf("FindAllStringIndex(%q, %q) = %v, regexp gives %v", c.pat, c.s, got, want)
		}
		if got, want := ours.FindAllString(c.s, -1), std.FindAllString(c.s, -1); !reflect.DeepEqual(got, want) {
			t.Errorf("FindAllString(%q, %q) = %q, regexp gives %q", c.pat, c.s, got, want)
		}
		if got, want := ours.FindAllStringSubmatchIndex(c.s, -1), std.FindAllStringSubmatchIndex(c.s, -1); !reflect.DeepEqual(got, want) {
			t.Errorf("FindAllStringSubmatchIndex(%q, %q) = %v, regexp gives %v", c.pat, c.s, got, want)
		}
		if got, want := ours.Split(c.s, -1), std.Split(c.s, -1); !reflect.DeepEqual(got, want) {
			t.Errorf("Split(%q, %q) = %q, regexp gives %q", c.pat, c.s, got, want)
		}
		// n as a cap must clip the FILTERED sequence, not the raw one.
		for _, n := range []int{1, 2, 3} {
			if got, want := ours.FindAllStringIndex(c.s, n), std.FindAllStringIndex(c.s, n); !reflect.DeepEqual(got, want) {
				t.Errorf("FindAllStringIndex(%q, %q, n=%d) = %v, regexp gives %v", c.pat, c.s, n, got, want)
			}
		}
	}
	if len(cases) != 11 {
		t.Fatalf("denominator changed: %d cases", len(cases)) // a deleted row must fail, not shrink
	}
}
