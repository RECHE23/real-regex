---
title: "real::regex — linear-time, ReDoS-safe regex for C++20"
---

```{raw} html
<div class="real-landing">
<header class="hero">
  <div class="hero__grid">
    <div class="hero__col">
      <span class="eyebrow">C++20 &nbsp;&middot;&nbsp; header-only &nbsp;&middot;&nbsp; constexpr</span>
      <h1>Regular expressions that can't blow up.</h1>
      <p class="hero__lead">A <b>linear-time</b>, <b>ReDoS-safe</b> regex engine for C++20 &mdash; and a faithful drop-in for <b>std::regex</b>, <b>RE2</b>, Python <span class="mono">re</span>, Rust <span class="mono">regex</span>, and Go <span class="mono">regexp</span>. Match untrusted input without ever falling off a backtracking cliff.</p>

      <div class="hero__cmd">
        <div class="cmd"><span class="prompt">$</span> brew install RECHE23/sci/real-regex</div>
      </div>
      <p class="hero__alt">or &nbsp;<b>pip install real-regex</b> &nbsp;&middot;&nbsp; <b>cargo add real-regex</b> &nbsp;&middot;&nbsp; <b>go get &hellip;/real-regex/bindings/go</b></p>

      <div class="hero__cta">
        <a class="btn btn--primary" href="#install">Get started &rarr;</a>
        <a class="btn btn--ghost" href="reference/index.html">API reference</a>
      </div>
    </div>

    <figure class="panel" aria-label="Runtime on hostile input: real stays linear while backtracking engines blow up exponentially">
      <figcaption class="panel__head">
        <span class="t">runtime vs. input length</span>
        <span class="pat mono">(a+)+$ &nbsp;&middot; worst case</span>
      </figcaption>
      <div class="chartbox"><canvas id="curve" width="520" height="220" role="img" aria-label="Two curves: real is a flat linear line; a backtracking engine rises as an exponential cliff"></canvas></div>
      <div class="legend">
        <span class="k"><span class="dot a"></span> real &mdash; O(n)</span>
        <span class="k"><span class="dot d"></span> backtracking &mdash; O(2&#8319;)</span>
      </div>
    </figure>
  </div>
</header>
</div>
```

```{raw} html
<div class="real-landing"><span class="eyebrow">what you get</span></div>
```

# A regex engine that trades nothing for safety.

::::{grid} 1 2 3 3
:gutter: 3

:::{grid-item-card} It can't blow up
{bdg-primary}`linear-time`

Every match is `O(n·m)`. No backtracking means no catastrophic path — ReDoS-safe by construction, not by heuristic or timeout.
:::

:::{grid-item-card} Compiles at compile time
{bdg-primary}`constexpr`

Build a `real::regex` in a `constexpr` context — the pattern is parsed and compiled before your program ever runs.
:::

:::{grid-item-card} Replace what you use
{bdg-primary}`drop-in`

Faithful drop-ins for `std::regex` and RE2 — same API, honest contracts, a linear-time engine underneath.
:::

:::{grid-item-card} Python, Rust, C, Go
{bdg-primary}`bindings`

One engine, idiomatic everywhere: drop into `re`, the `regex` crate, `regexp`, or call the frozen C ABI.
:::

:::{grid-item-card} Unicode, complete
{bdg-primary}`unicode`

UCD 16 `\p{}` properties, scripts, case-folding and word boundaries. Code-point mode by default, bytes mode when you ask.
:::

:::{grid-item-card} Header-only, nothing to link
{bdg-primary}`zero-dep`

A few dozen headers, no runtime dependency. Drop `include/` into any C++20 build and go.
:::

::::

```{raw} html
<div class="real-landing"><span class="eyebrow">the difference</span></div>
```

# One input. Two outcomes.

The pattern below is harmless-looking and perfectly valid. On a long run of `a`s ending
in the wrong byte, a backtracking engine explores an exponential number of paths. real
walks it once.

```{raw} html
<div class="real-landing">
<div class="cliff">
  <div>
    <div class="cliff__pat mono"><span class="hl">(a+)+</span>$ &nbsp;<span style="color:var(--ink-3)">matched against</span>&nbsp; "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!"</div>
    <p class="cliff__note">Same regex, same string. The only difference is whether the engine can be forced off a cliff by input it didn't choose &mdash; the difference between a feature and a denial-of-service.</p>
  </div>
  <div class="versus">
    <div class="vrow bad">
      <div class="who">std::regex / PCRE<small>backtracking</small></div>
      <div class="bar"><i></i></div>
      <div class="val">&asymp; 2&sup3;&#8304; steps</div>
    </div>
    <div class="vrow good">
      <div class="who">real<small>linear</small></div>
      <div class="bar"><i></i></div>
      <div class="val">&asymp; 30 steps</div>
    </div>
  </div>
</div>
</div>
```

```{raw} html
<div class="real-landing"><span class="eyebrow">quickstart</span></div>
```

# Familiar on the surface. Safe underneath.

If you know `std::regex`, `re`, the `regex` crate or `regexp`, you already know this API.

::::{tab-set}

:::{tab-item} C++

```{literalinclude} ../../examples/cpp/quickstart.cpp
:language: cpp
:start-after: [quickstart]
:end-before: [/quickstart]
:dedent: 2
```
:::

:::{tab-item} Python

```{literalinclude} ../../bindings/python/examples/quickstart.py
:language: python
:start-after: [quickstart]
:end-before: [/quickstart]
```
:::

:::{tab-item} Rust

```{literalinclude} ../../bindings/rust/examples/quickstart.rs
:language: rust
:start-after: [quickstart]
:end-before: [/quickstart]
:dedent: 4
```
:::

:::{tab-item} Go

```{literalinclude} ../../bindings/go/quickstart_example_test.go
:language: go
:lines: 23-24,34-35
```
:::

::::

*// every snippet verified against the current public API — see the {doc}`API reference <reference/index>` for `real::basic_regex` itself.*

```{raw} html
<div class="real-landing"><span class="eyebrow">install</span></div>
```

(install)=
# Four ecosystems, one engine.

::::{grid} 1 2 2 4
:gutter: 3

:::{grid-item-card} C++
header-only

```console
$ brew install RECHE23/sci/real-regex
# or: vendor include/ (header-only)
```
:::

:::{grid-item-card} Python
PyPI

```console
$ pip install real-regex
```
:::

:::{grid-item-card} Rust
crates.io

```console
$ cargo add real-regex
```
:::

:::{grid-item-card} Go
module

```console
$ go get github.com/RECHE23/real-regex/bindings/go
```
:::

::::

```{toctree}
:hidden:
:maxdepth: 1

reference/index
```
