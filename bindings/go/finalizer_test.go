package real

import (
	"runtime"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

// A temporary *Regexp or *RegexSet is unreachable once its handle is loaded for a cgo call, so its
// finalizer may run during that call. With the GC forced in a loop, the handle is freed and reused
// mid-match unless every method keeps the receiver alive past the call.
func TestFinalizerDoesNotFreeDuringCall(t *testing.T) {
	big := strings.Repeat("ab", 1<<19) // 1 MiB, no 'c': the first branch fails at every start
	var stop atomic.Bool
	done := make(chan struct{})
	go func() {
		defer close(done)
		for !stop.Load() {
			runtime.GC()
			time.Sleep(50 * time.Microsecond)
		}
	}()
	defer func() { stop.Store(true); <-done }()
	const pattern = `(?:a|b)*c|(?:ab)+$`
	for i := 0; i < 40; i++ {
		if !MustCompile(pattern).MatchString(big) {
			t.Fatalf("iteration %d: MatchString on a temporary Regexp answered false", i)
		}
		if loc := MustCompile(pattern).FindStringIndex(big); loc == nil || loc[0] != 0 {
			t.Fatalf("iteration %d: FindStringIndex on a temporary Regexp answered %v", i, loc)
		}
		set, err := CompileSet([]string{`(?:a|b)*c`, `(?:ab)+$`})
		if err != nil {
			t.Fatal(err)
		}
		if got := set.Matches([]byte(big)); len(got) != 2 || got[0] || !got[1] {
			t.Fatalf("iteration %d: Matches on a temporary RegexSet answered %v", i, got)
		}
	}
}
