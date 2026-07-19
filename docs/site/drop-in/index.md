<!--
drop-in/index.md -- doc-site P1 reorg: the Drop-in section landing. One section,
one question: "how do I adopt real in MY language?". The bindings ARE drop-ins
(decision Rene) -- each replaces its host language's regex API -- so they live
here, not as their own header item. std-regex-reference stays nested inside
std-regex-tour's own toctree (unchanged since P2a); this page's toctree only
adds the tour and the bindings.
-->

# Drop-in

real::regex replaces the regex engine you already use — same API, a
**linear-time, ReDoS-safe** engine underneath:

- **C++ / std::regex** — start with the {doc}`migration tour <std-regex-tour>`,
  then the {doc}`compatibility contract <std-regex-reference>`.
- **Python · Rust · Go · C** — each binding is a drop-in for its host regex API:
  {doc}`language bindings <../bindings/index>`.

```{toctree}
:hidden:
:maxdepth: 1

std-regex-tour
Bindings <../bindings/index>
```
