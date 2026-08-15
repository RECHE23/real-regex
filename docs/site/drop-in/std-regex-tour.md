<!--
The Drop-in target page for std::regex, on the shared per-target template
(verdict · adopt/swap · API offered · differences & limitations · comparison).
Site-owned: docs/std_regex_dropin.dox remains the /api rendition, but this page
is no longer its byte-mirror -- the template and the {include} slice below are
MyST-only. std-regex-reference.md stays the linked deep reference.
-->

# Drop-in for std::regex

**Full drop-in, per-pattern fallback** — the same `<regex>` surface; every pattern
REAL can prove identical runs on the linear, ReDoS-safe engine, everything else
falls back to `std::regex` at construction. **Never a silent divergence.**

(dropin-migrate)=
## Adopt / swap

Swap the include, alias the namespace, and your `std::regex` code compiles unchanged:

```cpp
#include <real/compat/std/regex.hpp>   // was: #include <regex>
namespace re = real::compat;    // then use re::regex / re::smatch / re::regex_search / …
```

Under the default strict policy every accepted pattern runs `regex_search` /
`regex_match` in time linear in the input — no accepted pattern can go
super-linear, where a `std::regex` blows up exponentially on `(a+)+b`.

## API offered

The types (`regex`, `smatch` / `cmatch`, `sub_match`, `match_results`, `regex_error`),
the free functions (`regex_search`, `regex_match`, `regex_replace`) and the iterators
(`regex_iterator`, `regex_token_iterator`) keep their `std::regex` signatures and
semantics — at the call sites only the `std::` qualifier changes to `re::`. The
ECMAScript default and all five POSIX grammars (`basic`/`extended`/`awk`/`grep`/`egrep`)
run on REAL when the pattern translates.

```cpp
re::regex  r(R"((\w+)=(\d+))");
re::smatch m;
std::string line = "answer=42";
if (re::regex_search(line, m, r)) {
  auto key = m[1].str();   // "answer"
  auto val = m[2].str();   // "42"
}
```

Which backend won, and why — introspection for performance debugging:

```cpp
r.uses_real();            // true  -> the linear REAL engine
r.uses_real_traversal();  // true  -> replace/iterate also run on REAL
r.nullable();             // true  -> can match empty; replace/iterators defer to std
r.mark_count();           // capture-group count (std::regex parity)
```

Object-level reference: {doc}`std::regex compatibility <../reference/compat-std>`.

## Differences & limitations

- **Per-pattern fallback.** A backreference, an oversized or unbounded lookaround, a
  non-ASCII `[...]` member — anything REAL cannot prove equivalent routes to
  `std::regex` transparently, at construction; `uses_real()` surfaces it. Every
  routing rule lives in the {doc}`compatibility reference <std-regex-reference>`.
- **Always std by construction:** `wregex` and any non-`char` `CharT` or custom
  traits; `collate`; `nosubs`. There is deliberately no switch to force REAL there —
  forcing could diverge, which the contract forbids.
- **`regex_replace` and the iterators compose O(n) operations** — quadratic worst
  case, never exponential; a nullable pattern's replace/iteration delegates to std.
- **One tolerated capture divergence** — a nullable loop's final empty iteration:
  {ref}`rationale <div_empty_iteration_capture>`. The exhaustive contract, including
  the platform-variant (MSVC) pins, is the
  {doc}`compatibility reference <std-regex-reference>`.

## Comparison

Four-engine numbers live in the
[performance ledger](https://github.com/RECHE23/real-regex/blob/main/docs/BENCHMARKS.md);
the reading is {doc}`Performance <../performance/index>`. Where a backtracker
is ReDoS-able on a crafted lookaround and RE2 refuses the pattern, REAL's
bounded lookarounds stay linear.

<!--
std-regex-reference is nested here (not a direct entry of contents.md's root
toctree) so the pydata header nav shows one "Drop-in" item, not two -- see
contents.md's own comment. It still renders in this page's sidebar.
-->

```{toctree}
:hidden:
:maxdepth: 1

std-regex-reference
```
