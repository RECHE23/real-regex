package real

// regexp is the oracle for the whole Regexp surface, not for four methods of it.
//
// TestEmptyMatchMatchesRegexp already asks regexp about the abutting-empty rule, on eleven
// hand-picked patterns across FindAllStringIndex / FindAllString / FindAllStringSubmatchIndex /
// Split. This package exports twenty-four methods. The rest -- every []byte variant, every
// single-match Find, Match, and ReplaceAll -- had no oracle at all, and the []byte and string
// halves of the API were never checked against EACH OTHER.
//
// The asymmetry that motivates the ReplaceAll rows: the FindAll family applies Go's
// drop-an-empty-match-abutting-the-previous rule in Go code, while ReplaceAll hands the whole job
// to real_sub across the C ABI, which counts matches the REAL/Python way. Whether those two agree
// is a question, not an assumption.

import (
	"bytes"
	"fmt"
	"reflect"
	"regexp"
	"sort"
	"strings"
	"testing"
)

// RE2-compatible by construction: a pattern regexp refuses has no oracle, and this file is about
// the surfaces rather than about which patterns compile. The empty-matchable ones carry the weight,
// since that is where Go's counting rule and REAL's part ways.
var diffPatterns = []string{
	"a", "ab", "[ab]+", "a|b", "abc",
	"x*", "a*", "b*", "", "a?", "[a]*", "a*?",
	"(a)", "(a)(b)", "(a)|(b)", "(a(b))", "(?P<x>a)", "(?P<x>a)(?P<y>b)",
	"(a|)", "()", "(a)*", "(a)?", "(x*)",
	"^a", "a$", "^", "$", "^a$",
	",", "(ab|)",
}

// Patterns that CONSUME an arbitrary codepoint. regexp is not an oracle for them on malformed input:
// RE2 substitutes U+FFFD for an invalid byte and matches it as one rune, while a malformed byte here
// is never a codepoint at all and no class accepts it -- the engine's documented rule, pinned in
// tests/unicode/test_utf8_malformed_matrix.cpp. Zero-width matches no longer diverge on such input,
// which is why only the consuming patterns need routing away from it.
var diffAnyPatterns = []string{".", "(.)", "(.)(.)", "[^a]"}

// `\w`, `\d`, `\s` and the word boundaries built on them are Unicode-aware here and ASCII-only
// under RE2 -- a DOCUMENTED divergence (README.md), so regexp is not an oracle for them on a
// non-ASCII subject. They are swept over ASCII subjects, where the two definitions coincide, and
// the divergence itself is pinned by TestDocumentedUnicodeClassDivergence rather than dropped: a
// known difference that stops happening is a doc that has gone stale.
var diffClassPatterns = []string{`\w+`, `\d`, `\s*`, `\b`, `\B`, `\w`, `\W`}

var diffSubjects = []string{
	"", "a", "b", "ab", "ba", "aab", "abc", "aaa", "abab", "banana",
	"axbxc", "a,b,,c,", "a b  c", "xaybz", "a\nb", "  a  ", "\n",
	"é", "a😀b", "Кот",
}

// The subset of diffSubjects that is pure ASCII, where a Unicode-aware class and an ASCII-only one
// cannot disagree.
var diffASCIISubjects = []string{
	"", "a", "b", "ab", "ba", "aab", "abc", "aaa", "abab", "banana",
	"axbxc", "a,b,,c,", "a b  c", "xaybz", "a\nb", "  a  ", "\n",
}

// []byte subjects a Go string literal cannot express, kept VALID: malformed UTF-8 is a second
// documented divergence (empty matches are codepoint-aligned here, byte-aligned under regexp) and
// belongs to TestMalformedUTF8AlignmentDivergence, not in the middle of a surface sweep where it
// would bury everything else.
var diffByteSubjects = [][]byte{
	{}, []byte("a"), []byte("ab"), []byte("axbxc"), []byte("é"), []byte("a b  c"),
	[]byte("a,b,,c,"), []byte("Кот"),
	{0xff}, {0x80, 0x80}, []byte("a\xffb"), []byte("a\x80b"), {0xe2, 0x82}, {0xc3},
	{0xf0, 0x9f, 0x98}, {0xbf, 'a'}, []byte("é\x80"),
}

// The valid-UTF-8 half of the list above, for the consuming patterns.
var diffValidByteSubjects = [][]byte{
	{}, []byte("a"), []byte("ab"), []byte("axbxc"), []byte("é"), []byte("a b  c"),
	[]byte("a,b,,c,"), []byte("Кот"),
}

// Same reason as diffASCIISubjects: a byte outside ASCII is where the two class definitions part.
var diffASCIIByteSubjects = [][]byte{
	{}, []byte("a"), []byte("ab"), []byte("axbxc"), []byte("a b"),
}

// A suite pairs patterns with the subjects regexp can actually answer for.
type diffSuite struct {
	patterns []string
	subjects []string
	bytes    [][]byte
}

var diffSuites = []diffSuite{
	{diffPatterns, diffSubjects, diffByteSubjects},
	{diffClassPatterns, diffASCIISubjects, diffASCIIByteSubjects},
	{diffAnyPatterns, diffSubjects, diffValidByteSubjects},
}

var diffCounts = []int{-1, 0, 1, 2, 3}

type failure struct {
	surface string
	pattern string
	subject string
	n       int
	got     interface{}
	want    interface{}
}

func (f failure) String() string {
	return fmt.Sprintf("%s(pattern=%q, subject=%q, n=%d): got %s, regexp gives %s",
		f.surface, f.pattern, f.subject, f.n, render(f.got), render(f.want))
}

// Newlines inline so one divergence stays one line in the test log.
func render(v interface{}) string {
	return strings.ReplaceAll(fmt.Sprintf("%#v", v), "\n", `\n`)
}

func TestFindFamilyMatchesRegexp(t *testing.T) {
	var fails []failure
	compared, skipped := 0, 0

	record := func(surface, pat, subj string, n int, got, want interface{}) {
		compared++
		if !reflect.DeepEqual(got, want) {
			fails = append(fails, failure{surface, pat, subj, n, got, want})
		}
	}

	for _, su := range diffSuites {
		for _, pat := range su.patterns {
			std, err := regexp.Compile(pat)
			if err != nil {
				skipped++ // no oracle: regexp declines what REAL accepts, swept elsewhere
				continue
			}
			ours, ourErr := Compile(pat)
			if ourErr != nil {
				// regexp accepts it and this package does not: a report, never a panic, and never a
				// silent skip -- the whole point of an oracle is that its acceptances are the floor.
				fails = append(fails, failure{"Compile", pat, "", 0, ourErr.Error(), "regexp accepts it"})
				continue
			}

			record("NumSubexp", pat, "", 0, ours.NumSubexp(), std.NumSubexp())
			record("SubexpNames", pat, "", 0, ours.SubexpNames(), std.SubexpNames())
			record("String", pat, "", 0, ours.String(), std.String())

			for _, subj := range su.subjects {
				b := []byte(subj)
				record("MatchString", pat, subj, 0, ours.MatchString(subj), std.MatchString(subj))
				record("Match", pat, subj, 0, ours.Match(b), std.Match(b))
				record("FindString", pat, subj, 0, ours.FindString(subj), std.FindString(subj))
				record("FindStringIndex", pat, subj, 0, ours.FindStringIndex(subj), std.FindStringIndex(subj))
				record("FindStringSubmatch", pat, subj, 0, ours.FindStringSubmatch(subj), std.FindStringSubmatch(subj))
				record("FindStringSubmatchIndex", pat, subj, 0,
					ours.FindStringSubmatchIndex(subj), std.FindStringSubmatchIndex(subj))
				record("Find", pat, subj, 0, ours.Find(b), std.Find(b))
				record("FindIndex", pat, subj, 0, ours.FindIndex(b), std.FindIndex(b))
				record("FindSubmatch", pat, subj, 0, ours.FindSubmatch(b), std.FindSubmatch(b))
				record("FindSubmatchIndex", pat, subj, 0, ours.FindSubmatchIndex(b), std.FindSubmatchIndex(b))

				for _, n := range diffCounts {
					record("FindAllString", pat, subj, n, ours.FindAllString(subj, n), std.FindAllString(subj, n))
					record("FindAllStringIndex", pat, subj, n,
						ours.FindAllStringIndex(subj, n), std.FindAllStringIndex(subj, n))
					record("FindAllStringSubmatch", pat, subj, n,
						ours.FindAllStringSubmatch(subj, n), std.FindAllStringSubmatch(subj, n))
					record("FindAllStringSubmatchIndex", pat, subj, n,
						ours.FindAllStringSubmatchIndex(subj, n), std.FindAllStringSubmatchIndex(subj, n))
					record("FindAll", pat, subj, n, ours.FindAll(b, n), std.FindAll(b, n))
					record("FindAllIndex", pat, subj, n, ours.FindAllIndex(b, n), std.FindAllIndex(b, n))
					record("FindAllSubmatch", pat, subj, n, ours.FindAllSubmatch(b, n), std.FindAllSubmatch(b, n))
					record("FindAllSubmatchIndex", pat, subj, n,
						ours.FindAllSubmatchIndex(b, n), std.FindAllSubmatchIndex(b, n))
					record("Split", pat, subj, n, ours.Split(subj, n), std.Split(subj, n))
				}
			}

			// The []byte subjects that no string literal can express.
			for _, b := range su.bytes {
				label := string(b)
				record("Match/bytes", pat, label, 0, ours.Match(b), std.Match(b))
				record("Find/bytes", pat, label, 0, ours.Find(b), std.Find(b))
				record("FindIndex/bytes", pat, label, 0, ours.FindIndex(b), std.FindIndex(b))
				record("FindSubmatchIndex/bytes", pat, label, 0,
					ours.FindSubmatchIndex(b), std.FindSubmatchIndex(b))
				for _, n := range diffCounts {
					record("FindAll/bytes", pat, label, n, ours.FindAll(b, n), std.FindAll(b, n))
					record("FindAllIndex/bytes", pat, label, n, ours.FindAllIndex(b, n), std.FindAllIndex(b, n))
					record("FindAllSubmatch/bytes", pat, label, n,
						ours.FindAllSubmatch(b, n), std.FindAllSubmatch(b, n))
					record("FindAllSubmatchIndex/bytes", pat, label, n,
						ours.FindAllSubmatchIndex(b, n), std.FindAllSubmatchIndex(b, n))
				}
			}
			ours.Close() // closed per pattern: a deferred close inside a loop holds all of them open
		}
	}

	t.Logf("find family: %d comparisons, %d patterns skipped (regexp declined)", compared, skipped)
	reportFailures(t, fails)
	if compared == 0 {
		t.Fatal("nothing was compared")
	}
}

// TestByteAndStringHalvesAgree needs no oracle: the two halves of the API are the same question
// asked twice, so they must give the same answer whatever that answer is. A divergence here is
// internal and cannot be blamed on regexp.
func TestByteAndStringHalvesAgree(t *testing.T) {
	var fails []failure
	compared := 0
	for _, su := range diffSuites {
		for _, pat := range su.patterns {
			if _, err := regexp.Compile(pat); err != nil {
				continue
			}
			ours, err := Compile(pat)
			if err != nil {
				continue // reported by TestFindFamilyMatchesRegexp; not this test's subject
			}
			for _, subj := range su.subjects {
				b := []byte(subj)
				compared++
				if got, want := ours.FindStringIndex(subj), ours.FindIndex(b); !reflect.DeepEqual(got, want) {
					fails = append(fails, failure{"FindStringIndex vs FindIndex", pat, subj, 0, got, want})
				}
				if got, want := ours.FindString(subj), string(ours.Find(b)); got != want {
					fails = append(fails, failure{"FindString vs Find", pat, subj, 0, got, want})
				}
				for _, n := range diffCounts {
					got := ours.FindAllStringIndex(subj, n)
					want := ours.FindAllIndex(b, n)
					if !reflect.DeepEqual(got, want) {
						fails = append(fails, failure{"FindAllStringIndex vs FindAllIndex", pat, subj, n, got, want})
					}
					gots := ours.FindAllString(subj, n)
					var wants []string
					for _, piece := range ours.FindAll(b, n) {
						wants = append(wants, string(piece))
					}
					if !reflect.DeepEqual(gots, wants) {
						fails = append(fails, failure{"FindAllString vs FindAll", pat, subj, n, gots, wants})
					}
				}
			}
			ours.Close()
		}
	}
	t.Logf("byte/string duality: %d subject pairs", compared)
	reportFailures(t, fails)
}

// TestReplaceAllMatchesRegexp translates the template rather than assuming the spellings match:
// this package takes REAL's `\1` / `\g<name>` where regexp takes `$1` / `${name}`, and refuses a
// dollar template outright. The MATCH SEQUENCE is the thing under test -- ReplaceAll crosses the C
// ABI instead of going through the filtered enumeration the FindAll family uses, so it is the one
// surface where Go's counting rule could quietly not apply.
func TestReplaceAllMatchesRegexp(t *testing.T) {
	var fails []failure
	compared := 0
	for _, su := range diffSuites {
		for _, pat := range su.patterns {
			std, err := regexp.Compile(pat)
			if err != nil {
				continue
			}
			ours, err := Compile(pat)
			if err != nil {
				continue // reported by TestFindFamilyMatchesRegexp
			}
			groups := std.NumSubexp()

			type tmpl struct{ ours, theirs string }
			templates := []tmpl{{"-", "-"}, {"", ""}, {"<>", "<>"}, {`[\g<0>]`, "[${0}]"}}
			if groups >= 1 {
				templates = append(templates, tmpl{`\1`, "${1}"}, tmpl{`x\1y`, "x${1}y"})
			}
			if groups >= 2 {
				templates = append(templates, tmpl{`\2-\1`, "${2}-${1}"})
			}
			for _, name := range std.SubexpNames() {
				if name != "" {
					templates = append(templates, tmpl{`\g<` + name + `>`, "${" + name + "}"})
				}
			}

			for _, subj := range su.subjects {
				for _, tp := range templates {
					got, err := ours.ReplaceAll([]byte(subj), []byte(tp.ours))
					compared++
					if err != nil {
						fails = append(fails, failure{"ReplaceAll/error", pat, subj, 0, err.Error(), tp.theirs})
						continue
					}
					want := std.ReplaceAll([]byte(subj), []byte(tp.theirs))
					if !bytes.Equal(got, want) {
						fails = append(fails, failure{"ReplaceAll[" + tp.ours + "]", pat, subj, 0,
							string(got), string(want)})
					}
				}
			}
			ours.Close()
		}
	}
	t.Logf("ReplaceAll: %d comparisons", compared)
	reportFailures(t, fails)
}

func reportFailures(t *testing.T, fails []failure) {
	t.Helper()
	if len(fails) == 0 {
		return
	}
	// The tally comes first and is not truncated. A capped list of examples answers "what does one
	// look like"; only the tally answers "is this ONE family or several", which is the question that
	// decides whether a divergence is a known flavor difference or something new hiding behind it.
	byPattern := map[string]int{}
	order := []string{}
	for _, f := range fails {
		if byPattern[f.pattern] == 0 {
			order = append(order, f.pattern)
		}
		byPattern[f.pattern]++
	}
	sort.Slice(order, func(i, j int) bool { return byPattern[order[i]] > byPattern[order[j]] })
	tally := make([]string, 0, len(order))
	for _, pat := range order {
		tally = append(tally, fmt.Sprintf("%q×%d", pat, byPattern[pat]))
	}
	t.Errorf("%d divergence(s) against regexp across %d pattern(s): %s",
		len(fails), len(order), strings.Join(tally, " "))

	const shown = 12
	for i, f := range fails {
		if i == shown {
			t.Errorf("... %d more divergence(s)", len(fails)-shown)
			break
		}
		t.Error(f.String())
	}
}

// TestDocumentedUnicodeClassDivergence pins the difference README.md advertises: `\w`, `\d`, `\s`
// and the boundaries built on them are Unicode-aware here and ASCII-only under RE2. The sweeps
// above route those patterns away from non-ASCII subjects, which would otherwise bury a real
// finding under a known one -- and a routed-around divergence that silently stopped happening would
// leave the README asserting something false. So it is asserted, not avoided.
func TestDocumentedUnicodeClassDivergence(t *testing.T) {
	cases := []struct {
		pattern string
		subject string
	}{
		{`\w+`, "café"}, // README's own example: regexp matches "caf", this package matches all of it
		{`\w`, "é"},
		{`\w+`, "Кот"},
		{`\d`, "٣"}, // ARABIC-INDIC DIGIT THREE
		{`\s`, " "},
	}
	for _, c := range cases {
		std := regexp.MustCompile(c.pattern)
		ours := MustCompile(c.pattern)
		defer ours.Close()
		got, want := ours.FindString(c.subject), std.FindString(c.subject)
		if got == want {
			t.Errorf("%q on %q: both give %q — README.md still claims this package is "+
				"Unicode-aware where regexp is ASCII-only; one of the two has changed",
				c.pattern, c.subject, got)
		}
		if got == "" {
			t.Errorf("%q on %q: this package matched nothing; the divergence is supposed to be "+
				"that it matches MORE than regexp, not less", c.pattern, c.subject)
		}
	}
	if len(cases) != 5 {
		t.Fatalf("denominator changed: %d cases", len(cases))
	}
}

// TestMalformedUTF8ConsumingDivergence pins the divergence the surface sweep routes its consuming
// patterns around: RE2 substitutes U+FFFD for an invalid byte and matches it as one rune, while a
// malformed byte is never a codepoint here and no class accepts it.
//
// Asserted rather than avoided, for the same reason as the Unicode-class divergence: a difference
// that is routed around and then silently stops happening leaves the documentation asserting
// something false. Both directions are checked, so this fails whichever side moves.
func TestMalformedUTF8ConsumingDivergence(t *testing.T) {
	consuming := []string{".", "(.)", "[^a]"}
	subjects := [][]byte{{0xff}, {0x80}, {0xc0}, {0xe2, 0x82}}
	std := map[string]*regexp.Regexp{}
	for _, p := range consuming {
		std[p] = regexp.MustCompile(p)
	}
	for _, p := range consuming {
		ours := MustCompile(p)
		for _, b := range subjects {
			if got := ours.FindIndex(b); got != nil {
				t.Errorf("%q on %#v: this package matched %v; a malformed byte is not a codepoint here",
					p, b, got)
			}
			if got := std[p].FindIndex(b); got == nil {
				t.Errorf("%q on %#v: regexp matched nothing; the divergence this pins has moved", p, b)
			}
		}
		ours.Close()
	}

	// ZERO-WIDTH matches do NOT diverge on the same input, which is what makes the line above a
	// statement about consuming and not about "malformed" in general.
	star, anchor := MustCompile("x*"), MustCompile("^")
	defer star.Close()
	defer anchor.Close()
	stdStar, stdAnchor := regexp.MustCompile("x*"), regexp.MustCompile("^")
	for _, b := range append(subjects, []byte{0x80, 0x80}, []byte("a\x80b")) {
		if got, want := star.FindAllIndex(b, -1), stdStar.FindAllIndex(b, -1); !reflect.DeepEqual(got, want) {
			t.Errorf("x* on %#v: %v vs regexp %v — zero-width matches are supposed to AGREE", b, got, want)
		}
		if got, want := anchor.FindAllIndex(b, -1), stdAnchor.FindAllIndex(b, -1); !reflect.DeepEqual(got, want) {
			t.Errorf("^ on %#v: %v vs regexp %v", b, got, want)
		}
	}

	if len(consuming) != 3 || len(subjects) != 4 {
		t.Fatalf("denominator changed: %d patterns, %d subjects", len(consuming), len(subjects))
	}
}
