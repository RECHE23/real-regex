# real-regex

A drop-in replacement for Python's `re`, backed by a linear-time, **ReDoS-safe** C++20 engine — with
**bounded lookarounds** that the other linear-time engines (RE2, Rust) don't have.

```bash
pip install real-regex
```

## Drop-in for `re`

Import it as `re` and use the API you already know — `compile`, `search`, `match`, `fullmatch`, `findall`,
`finditer`, `sub`, `subn`, `split`, and `Match.group` / `groups` / `groupdict` / `span` / `start` / `end`:

```python
import real as re

m = re.match(r"(\w+)@(\w+)\.(\w+)", "user@example.com")
m.group(1), m.group(2), m.group(3)          # ('user', 'example', 'com')

re.findall(r"\d+", "a1 b22 c333")            # ['1', '22', '333']
re.sub(r"\s+", "_", "a  b   c")              # 'a_b_c'
```

Flags work the same: `re.I`, `re.M`, `re.S`, `re.X`, `re.A`. Unicode `\w \d \s \b` and `IGNORECASE`
case-folding follow `re` in text mode.

## Why replace `re`

- **No ReDoS.** `re` (like every backtracking engine) blows up on patterns like `(a+)+b` against a hostile
  input — seconds, then minutes. real-regex is a Thompson NFA simulation: **linear time, always**, no
  catastrophic backtracking, ever.
- **Bounded lookarounds, still linear.** `(?=…)` `(?!…)` `(?<=…)` `(?<!…)` — including variable-width
  lookbehind — match without backtracking. RE2 and Rust drop lookarounds to stay safe; real-regex keeps them.
- **Fast on the common cases.** One-pass extraction and a lazy DFA put capture-dense `finditer` and anchored
  `fullmatch` ahead of `re`, and validation/no-match scans several times faster. It is not the fastest engine
  at raw throughput (a JIT wins there) — it is the safe one that is also quick.

## Not `re`, on purpose

**Strict by default — every accepted pattern is guaranteed linear.** The unsupported constructs
(backreferences, conditionals, Unicode property classes `\p{…}`) raise `real.error` rather than silently
falling back to a backtracking engine, so a compiled pattern is a ReDoS-safety guarantee, not a maybe. Opt
into delegating those to the standard library `re` per call with `real.compile(pat, fallback=True)` (or
module-wide with `real.fallback = True`) — it may accept them, at the cost of the linear-time guarantee for
that pattern; `Pattern.engine` (`"real"` or `"re"`) always tells you which backend you got. A few semantics
differ deliberately (e.g. a nullable loop's final empty capture). Full list:
[COMPATIBILITY.md](https://github.com/RECHE23/real-regex/blob/main/docs/COMPATIBILITY.md).

The wheel also ships the header-only C++ library — `real.get_include()` returns its path, so the same engine
is available to C++ via `#include <real/real.hpp>`. Source, benchmarks and the C++ API:
<https://github.com/RECHE23/real-regex>.
