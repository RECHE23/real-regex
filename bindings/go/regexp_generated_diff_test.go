package real

// regexp as an oracle over generated patterns, where the curated sweeps above check named surfaces.
//
// The generator draws RE2-compatible patterns (classes, quantifiers lazy and greedy, groups, alternations,
// anchors, word boundaries, (?i) (?m) (?s)) over a three-letter alphabet with newlines and spaces, so
// literals, classes and repetitions overlap all the time. Subjects are ASCII: a Unicode-aware `\w` and
// RE2's ASCII one cannot disagree there (that divergence is TestDocumentedUnicodeClassDivergence's).
//
// One family is documented as different and told apart by the pattern's structure, not by its answers:
// a repeat that can iterate more than once over a body that can match empty (the nullable loop; see
// differences-from-re, div_empty_iteration_capture). Its divergences are counted, not failed. Any other
// divergence fails, and a run that perturbs REAL's answers must be refused, so a green says the
// comparison can still see a wrong answer.
//
// REAL_GO_DIFF_SEEDS (space-separated) and REAL_GO_DIFF_PATTERNS size the run; the weekly long run widens
// both. A failure names its seed, which reproduces it.

import (
	"fmt"
	"math/rand"
	"os"
	"reflect"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

type genPiece struct {
	text     string
	nullable bool
}

type regexpGenerator struct {
	rnd          *rand.Rand
	nullableLoop bool
}

func (g *regexpGenerator) quantified(base genPiece) genPiece {
	q := []string{"", "", "", "+", "*", "?", "{1,2}", "{2}", "+?", "*?", "??", "{0,2}"}[g.rnd.Intn(12)]
	if q == "" {
		return base
	}
	repeats := q != "?" && q != "??"
	if repeats && base.nullable {
		g.nullableLoop = true
	}
	zeroMin := q[0] == '*' || q[0] == '?' || strings.HasPrefix(q, "{0")
	return genPiece{base.text + q, base.nullable || zeroMin}
}

func (g *regexpGenerator) atom(depth int) genPiece {
	n := 14
	if depth < 2 {
		n = 18
	}
	switch g.rnd.Intn(n) {
	case 0, 1, 2:
		lit := []string{"a", "b", "x", "ab", "ba", `\n`}[g.rnd.Intn(6)]
		if len(lit) == 1 {
			return g.quantified(genPiece{lit, false})
		}
		return genPiece{lit, false}
	case 3:
		return g.quantified(genPiece{"[ab]", false})
	case 4:
		return g.quantified(genPiece{"[^a]", false})
	case 5:
		return g.quantified(genPiece{".", false})
	case 6:
		return genPiece{`\b`, true}
	case 7:
		return genPiece{`\B`, true}
	case 8:
		return genPiece{"^", true}
	case 9:
		return genPiece{"$", true}
	case 10:
		return genPiece{`\A`, true}
	case 11:
		return genPiece{`\z`, true}
	case 12, 13:
		return g.quantified(genPiece{"x", false})
	case 14, 15:
		body := g.alternation(depth + 1)
		return g.quantified(genPiece{"(" + body.text + ")", body.nullable})
	default:
		body := g.alternation(depth + 1)
		return g.quantified(genPiece{"(?:" + body.text + ")", body.nullable})
	}
}

func (g *regexpGenerator) sequence(depth int) genPiece {
	out := genPiece{"", true}
	for i := 1 + g.rnd.Intn(3); i > 0; i-- {
		p := g.atom(depth)
		out.text += p.text
		out.nullable = out.nullable && p.nullable
	}
	return out
}

func (g *regexpGenerator) alternation(depth int) genPiece {
	out := g.sequence(depth)
	if g.rnd.Intn(3) == 0 {
		other := g.sequence(depth)
		out.text += "|" + other.text
		out.nullable = out.nullable || other.nullable
	}
	return out
}

// pattern draws one pattern and reports whether it holds a nullable loop.
func (g *regexpGenerator) pattern() (string, bool) {
	g.nullableLoop = false
	flags := []string{"", "(?i)", "(?m)", "(?s)", "(?ms)"}[g.rnd.Intn(5)]
	return flags + g.alternation(0).text, g.nullableLoop
}

func (g *regexpGenerator) subject() string {
	b := make([]byte, g.rnd.Intn(10))
	for i := range b {
		b[i] = "abxab\n "[g.rnd.Intn(7)]
	}
	return string(b)
}

type generatedRun struct {
	patterns, refusedByREAL, compared, nullableDivergences int
	failures                                               []string
}

// runGenerated compares one seed's patterns. perturb moves REAL's first non-empty match by a byte on
// every pattern without a nullable loop, for the can-fail check.
func runGenerated(t *testing.T, seed int64, patterns int, perturb bool) generatedRun {
	t.Helper()
	g := &regexpGenerator{rnd: rand.New(rand.NewSource(seed))}
	var run generatedRun
	for i := 0; i < patterns; i++ {
		pat, nullable := g.pattern()
		std, err := regexp.Compile(pat)
		if err != nil {
			continue
		}
		run.patterns++
		ours, err := Compile(pat)
		if err != nil {
			run.refusedByREAL++
			run.failures = append(run.failures, fmt.Sprintf("seed %d: REAL refuses %q, which regexp accepts: %v", seed, pat, err))
			continue
		}
		for s := 0; s < 10; s++ {
			subj := g.subject()
			run.compared++
			got := map[string]interface{}{
				"FindAllStringSubmatchIndex": ours.FindAllStringSubmatchIndex(subj, -1),
				"FindStringSubmatchIndex":    ours.FindStringSubmatchIndex(subj),
				"MatchString":                ours.MatchString(subj),
			}
			want := map[string]interface{}{
				"FindAllStringSubmatchIndex": std.FindAllStringSubmatchIndex(subj, -1),
				"FindStringSubmatchIndex":    std.FindStringSubmatchIndex(subj),
				"MatchString":                std.MatchString(subj),
			}
			if first, ok := got["FindStringSubmatchIndex"].([]int); perturb && !nullable && ok && first != nil && first[1] > first[0] {
				first[1]--
			}
			for surface, w := range want {
				if reflect.DeepEqual(got[surface], w) {
					continue
				}
				if nullable {
					run.nullableDivergences++
				} else {
					run.failures = append(run.failures, fmt.Sprintf("seed %d: %s(%q, %q): got %s, regexp gives %s",
						seed, surface, pat, subj, render(got[surface]), render(w)))
				}
				break
			}
		}
		ours.Close()
	}
	return run
}

// The default seeds are ones whose patterns reach the nullable-loop family, so the default run exercises
// the exemption it grants rather than holding it unused.
var defaultGeneratedSeeds = []int64{4, 6, 8}

func generatedRunSize(t *testing.T) ([]int64, int, bool) {
	t.Helper()
	seeds, defaults := defaultGeneratedSeeds, true
	if v := os.Getenv("REAL_GO_DIFF_SEEDS"); v != "" {
		defaults = false
		seeds = nil
		for _, f := range strings.Fields(v) {
			s, err := strconv.ParseInt(f, 10, 64)
			if err != nil {
				t.Fatalf("REAL_GO_DIFF_SEEDS: %v", err)
			}
			seeds = append(seeds, s)
		}
	}
	patterns := 5000
	if v := os.Getenv("REAL_GO_DIFF_PATTERNS"); v != "" {
		n, err := strconv.Atoi(v)
		if err != nil {
			t.Fatalf("REAL_GO_DIFF_PATTERNS: %v", err)
		}
		patterns = n
		defaults = false
	}
	return seeds, patterns, defaults
}

func TestGeneratedPatternsMatchRegexp(t *testing.T) {
	seeds, patterns, defaults := generatedRunSize(t)
	nullable := 0
	for _, seed := range seeds {
		run := runGenerated(t, seed, patterns, false)
		nullable += run.nullableDivergences
		t.Logf("seed %d: %d patterns, %d subjects compared, %d divergences in the nullable-loop family",
			seed, run.patterns, run.compared, run.nullableDivergences)
		for i, f := range run.failures {
			if i == 20 {
				t.Errorf("... %d more", len(run.failures)-20)
				break
			}
			t.Error(f)
		}
		// A generator that stopped drawing compilable patterns would leave this vacuously green.
		if run.patterns < patterns/2 {
			t.Errorf("seed %d: only %d of %d patterns compiled under regexp", seed, run.patterns, patterns)
		}
	}
	// The family is documented as a difference from regexp. Were it to stop diverging, the documentation
	// would be the stale part, and the exemption would have nothing left to exempt.
	if defaults && nullable == 0 {
		t.Error("the default seeds no longer reach a nullable-loop divergence: update the documented difference")
	}
}

func TestGeneratedComparisonRefusesAWrongAnswer(t *testing.T) {
	run := runGenerated(t, 1, 300, true)
	if len(run.failures) == 0 {
		t.Fatal("REAL's answers were moved and the comparison accepted every one")
	}
}
