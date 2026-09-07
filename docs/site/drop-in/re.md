<!--
The Drop-in target page for Python's re, on the shared per-target template.
Sources: bindings/python/README.md (the binding's canon) and the
differences-from-re page (re-semantics canon) -- linked, not copied.
-->

# Drop-in for Python `re`

**Full drop-in, strict by default** — `import real as re` and use the API you
already know; every accepted pattern is guaranteed linear. An unsupported
construct raises `real.error` instead of silently backtracking.

## Adopt / swap

```python
import real as re   # drop-in for the standard library's re

m = re.search(r"(\w+)@(\w+)", "info@example.com")
m.group(2)          # 'example' — linear time, no backtracking cliff
```

## API offered

The `re` surface you already call is offered whole — the module functions,
`Pattern`'s methods, `Match`'s accessors — same names, same shapes.
Flags `re.I`, `re.M`, `re.S`, `re.X`, `re.A` behave the same; Unicode
`\w \d \s \b` and `IGNORECASE` folding follow `re` in text mode.

Beyond `re` — flagged extensions, never silent divergences:

- `Pattern.count_matches(text)` — count matches without building `Match` objects.
- `real.compile(pat, fallback=True)` (or module-wide `real.fallback = True`) —
  delegate a rejected pattern to stdlib `re` for that pattern, trading its
  linear-time guarantee; `Pattern.engine` says which backend ran.
- `\p{…}` property classes, the `\N{U+XXXX}` scalar escape, and `\u{…}` (a
  synonym of `\x{…}`) — supersets that stdlib `re` rejects
  ({doc}`why <../differences-from-re>`).

Object-level reference: {doc}`Python API <../reference/python>`.

## Differences & limitations

Every intentional divergence from `re` — semantics, rationale, pins — lives in
{doc}`Differences from Python re <../differences-from-re>`. The one
binding-specific line: `\N{NAME}` is resolved by Python's `unicodedata`, so
character-name lookup exists only on the Python surface (no C++ name table).

**Deeply nested patterns fail differently, and that changes what you catch.**
REAL caps parser nesting at **200** groups: 200 compiles, 201 raises
`real.error` with `msg='pattern nesting too deep'` and `pos=200`, so it arrives
through the same `except real.error` as every other pattern fault and carries a
position. `re` has no cap — it recurses until the interpreter's stack runs out
and raises **`RecursionError`**, which is not a `re.error` and carries no
position, so `except re.error` does not catch it.

No depth is quoted for `re` because it does not have one: its boundary follows
`sys.getrecursionlimit()`. Measured at the default 1000, `re` compiled 400
nested groups and failed at 500; raised to 3000 it compiled 900, while REAL's
200 did not move. Rationale and both measurements:
{ref}`Nesting depth <div_nesting>`.

## Comparison

Python `re` is a backtracker. REAL is linear on every accepted pattern. The
shared binding bench — same harness, match counts checked before timing — lives
in the
[performance ledger](https://github.com/RECHE23/real-regex/blob/main/docs/BENCHMARKS.md);
the reading is {doc}`Performance <../performance/index>`.
