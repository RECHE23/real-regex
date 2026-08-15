<!--
The Developer section landing: an orientation hub that LINKS the existing
developer-facing artifacts -- nothing is relocated or rewritten here. The three
/api links are raw HTML: Doxygen output lives outside the Sphinx doctree, so a
MyST link would fail under -W.
-->

# Developer

Everything for reading, extending, and trusting the engine.

- **Symbol reference** — the exhaustive, per-symbol Doxygen rendition of the
  headers: <a href="../api/index.html">/api</a>.
- **Architecture** — {doc}`How REAL works <how-it-works>`, then the full
  guided tour on <a href="../api/design.html">/api/design.html</a>
  (Thompson NFA, Pike VM, prefilter, every fast path).
- **Coverage** — <a href="../api/coverage.html">the coverage report</a>: LLVM
  source-based instrumentation, how to read it, and the full per-line report.
- **Performance** — the capability picture and how to read a number:
  {doc}`Performance <../performance/index>`. Tables and the measurement
  doctrine stay on GitHub:
  [BENCHMARKS.md](https://github.com/RECHE23/real-regex/blob/main/docs/BENCHMARKS.md),
  [MEASUREMENT.md](https://github.com/RECHE23/real-regex/blob/main/docs/MEASUREMENT.md).
- **Testing** — the test taxonomy and its gates:
  [TESTS.md](https://github.com/RECHE23/real-regex/blob/main/docs/TESTS.md).
- **Development workflow** — the build/test harness (SciForge) and the
  make-target taxonomy: {doc}`Development workflow <workflow>`.

```{toctree}
:hidden:
:maxdepth: 1

how-it-works
workflow
```
