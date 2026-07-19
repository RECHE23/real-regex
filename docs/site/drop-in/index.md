<!--
The Drop-in section landing: "how do I adopt real in MY language?". The bindings
are drop-ins too, so they nest here. std-regex-reference nests inside the tour's
own toctree, not this one.
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
