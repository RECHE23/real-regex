<!--
The Drop-in section landing: "how do I adopt real in MY language?". Two groups:
C++ engine replacements and language bindings, each a drop-in for its host's
regex API. The bindings umbrella stays linked until every language has its own
target page. The C shim has no host regex API to replace, so it is not a
drop-in target; its surface belongs to the API reference.
-->

# Drop-in

real::regex replaces the regex engine you already use — same API, a
**linear-time, ReDoS-safe** engine underneath.

**C++ engine replacements**

- {doc}`std::regex <std-regex-tour>` — the `<regex>` surface with per-pattern
  fallback; the {doc}`compatibility reference <std-regex-reference>` holds the
  exhaustive contract.
- RE2 — `real::compat::re2`, the RE2-shaped surface on the same linear engine;
  the syntax contract is documented at the top of `<real/compat/re2/re2.hpp>`.

**Language bindings** — each a drop-in for its host's regex API:

- Python `re` · Rust `regex` crate · Go `regexp` —
  {doc}`language bindings <../bindings/index>`.

```{toctree}
:hidden:
:maxdepth: 1

std::regex <std-regex-tour>
Bindings <../bindings/index>
```
