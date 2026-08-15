<!--
Three-to-four-screen tour extracted from docs/design.dox. The full guided
tour — every fast path, the architecture map, the measured characteristics —
stays at /api/design.html (it \ref's real::detail, so it belongs on the
developer Doxygen tree). Do not copy that file here.
-->

# How REAL works

REAL is a linear-time (ReDoS-safe), `constexpr`, header-only regex engine: a
Thompson NFA simulated by a Pike VM, accelerated by a literal prefilter and
a handful of whole-pattern fast paths.

This page is the map. The guided tour with the data structures, the Unicode
story and every fast path is
<a href="../api/design.html">How REAL Works (/api)</a>.

## Two families

A regex denotes a *set* of strings; matching asks whether, and where, some
text belongs to that set. Running time on untrusted input is a security
property.

- **Backtracking** (Perl, PCRE, Python `re`, `std::regex`): try one
  alternative, rewind on failure, try the next. On `(a+)+b` over *n*
  copies of `a` with no `b`, the work is exponential. An attacker who
  controls a pattern or an input weaponizes this — ReDoS.
- **Automaton simulation** (RE2, Rust's `regex`, REAL): track *all* the
  ways the pattern could match so far, advancing them together one
  character at a time. It never rewinds. Time is linear in the input for
  *every* accepted pattern.

REAL belongs to the second family. Linear time is a guarantee by
construction. Bounded lookarounds and possessive quantifiers stay inside
that bound; backreferences are refused up front.

## A pattern's path

1. **Parse.** A recursive-descent parser turns the pattern into an index-pool
   syntax tree (children are integers, not pointers — so the tree is a
   `constexpr` value). Scoped flags (`(?i:…)`) stamp each node with the
   flags in force where it was parsed.
2. **Compile.** Thompson construction emits an NFA program: a flat bytecode
   of splits, jumps, character classes and captures. An invalid pattern
   throws `real::regex_error` with a byte offset, or is a compile error on
   `static_regex`.
3. **Match.** A Pike VM simulates the NFA. At each input position it holds
   the set of live threads; each thread is a program counter plus capture
   slots. No thread is ever replayed, so the work per byte is bounded by
   the program size.

A compiled regex is immutable and shareable across threads. An iterator is
not.

## Fast paths, not shortcuts

Most real-world patterns never walk the general VM. A required-literal
prefilter rejects a miss with a `memchr` (the classic ReDoS shape
`(a+)+b` dies on the missing `b` before the VM runs). Whole-pattern
shapes — a greedy class, a literal, a small alternation — have dedicated
loops. None of them is allowed to be faster *and* wrong: match counts
are checked against the VM before a number is published.

The list of those shapes, and when the general VM still wins, is §7 of
the <a href="../api/design.html">guided tour</a>.

## Read more

- {doc}`Features <../features>` — every construct the engine accepts or
  refuses.
- {doc}`Performance <../performance/index>` — the capability picture; the
  measured ledger stays on GitHub.
- <a href="../api/coverage.html">Coverage</a> — how to read the line
  report.
- {doc}`Development workflow <workflow>` — the make-target taxonomy.
