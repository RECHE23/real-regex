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
- {doc}`RE2 <re2>` — `real::compat::re2`, the RE2-shaped surface on the same
  linear engine, header-only and zero-dep.

**Language bindings** — each a drop-in for its host's regex API:

- {doc}`Python re <re>` — `import real as re`, strict by default.
- {doc}`Rust regex crate <regex>` — `use real_regex::Regex`, same surface.
- {doc}`Go regexp <go>` — `real.MustCompile`, v0.1 subset.

```{toctree}
:hidden:
:maxdepth: 1

std::regex <std-regex-tour>
RE2 <re2>
Python re <re>
Rust regex <regex>
Go regexp <go>
```
