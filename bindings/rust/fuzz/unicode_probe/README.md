# `unicode-probe` — the `\w`/`\s`/`\d` divergence audit

A standalone, manual tool. It asks both engines — `real-regex` (Python `re` semantics, its contract) and the
`regex` crate (UTS#18) — to classify every one of the 1,112,064 Unicode scalars with `^\w$`, `^\s$`, `^\d$`,
and reports the code points where they disagree, counted by Unicode general category. Because it asks the
engines rather than a hardcoded table, it self-updates with their Unicode version.

```
cargo run --release
```

Current result (the source of the README "Divergences" table):

```
\w:
  regex-only (UTS#18 ⊃), 2642 pts: {Cf: 2 (ZWNJ/ZWJ), Mc: 468, Me: 13, Mn: 2020, Pc: 9, So: 130}
  REAL-only  (CPython ⊃),  915 pts: {No: 915}
\s:
  regex-only (UTS#18 ⊃),    0 pts
  REAL-only  (CPython ⊃),    4 pts: {Cc: 4}  — U+001C-U+001F (CPython's str.isspace)
\d: 0 pts both ways — identical (both \p{Nd})
```

Re-run on a `regex` / `real-regex` Unicode-version bump; if the counts move, update the README table. The
fuzzer's word/space delta mask is already recomputed from the engines (`fuzz/fuzz_targets/differential.rs`), so
it tracks this probe automatically.
