package real

import (
	"reflect"
	"regexp"
	"testing"
)

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

func TestCloseIsIdempotent(t *testing.T) {
	r := MustCompile(`x`)
	r.Close()
	r.Close() // must not double-free / crash
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

// --- submatch / named-group / unset matrix (new in D1) --------------------------------------

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
