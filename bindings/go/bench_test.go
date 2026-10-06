package real

import (
	"bytes"
	"strings"
	"testing"
)

// A 1 MiB subject whose only match is at its start: what a query costs beyond the scan it needs. Each
// call used to copy the whole subject into C memory first.
func benchSubject() []byte {
	var b bytes.Buffer
	b.WriteString("needle ")
	for b.Len() < 1<<20 {
		b.WriteString("the quick brown fox jumps over the lazy dog ")
	}
	return b.Bytes()
}

func BenchmarkMatchEarlyHit(b *testing.B) {
	re := MustCompile("needle")
	text := benchSubject()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if !re.Match(text) {
			b.Fatal("no match")
		}
	}
}

func BenchmarkFindSubmatchIndexEarlyHit(b *testing.B) {
	re := MustCompile(`(\w+) (\w+)`)
	text := benchSubject()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if re.FindSubmatchIndex(text) == nil {
			b.Fatal("no match")
		}
	}
}

// A walk over many matches crosses cgo once per batch: dense words, groups, a sparse subject and a capped
// walk, each against regexp's own sequence (the differential tests hold the answers).
var findAllSubject = []byte(strings.Repeat("the quick brown fox x=1 jumps over 42 lazy dogs ", 400))

func benchFindAll(b *testing.B, pattern string, n int) {
	r := MustCompile(pattern)
	defer r.Close()
	b.SetBytes(int64(len(findAllSubject)))
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if r.FindAllSubmatchIndex(findAllSubject, n) == nil {
			b.Fatal("no match")
		}
	}
}

func BenchmarkFindAllWords(b *testing.B)  { benchFindAll(b, `[a-z]+`, -1) }
func BenchmarkFindAllGroups(b *testing.B) { benchFindAll(b, `(\w)=(\d+)`, -1) }
func BenchmarkFindAllSparse(b *testing.B) { benchFindAll(b, `lazy dogs the`, -1) }
func BenchmarkFindAllCapped(b *testing.B) { benchFindAll(b, `[a-z]+`, 3) }

// ReplaceAll expands every match in one call, the template parsed once.
func benchReplaceAll(b *testing.B, pattern, repl string) {
	r := MustCompile(pattern)
	defer r.Close()
	b.SetBytes(int64(len(findAllSubject)))
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if _, err := r.ReplaceAll(findAllSubject, []byte(repl)); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkReplaceAllWords(b *testing.B)  { benchReplaceAll(b, `[a-z]+`, "X") }
func BenchmarkReplaceAllGroups(b *testing.B) { benchReplaceAll(b, `(\w)=(\d+)`, `\2:\1`) }
