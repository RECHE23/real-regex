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
- `\p{…}` property classes and the `\N{U+XXXX}` scalar escape — supersets that
  stdlib `re` rejects ({doc}`why <../differences-from-re>`).

Object-level reference: {doc}`Python API <../reference/python>`.

## Differences & limitations

Every intentional divergence from `re` — semantics, rationale, pins — lives in
{doc}`Differences from Python re <../differences-from-re>`. The one
binding-specific line: `\N{NAME}` is resolved by Python's `unicodedata`, so
character-name lookup exists only on the Python surface (no C++ name table).

## Comparison

From the shared binding benchmark — full tables and method in
{doc}`Performance <../performance/index>`:

```{include} ../../BENCHMARKS.md
:start-after: "**~3×10⁶×** (ReDoS) |"
:end-before: "### finditer memory"
```
