package real

import (
	"bytes"
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
