# real-regex rebar runner

A [rebar](https://github.com/BurntSushi/rebar) engine runner for the `real-regex` crate — it lets REAL be
measured inside the standard cross-engine benchmark harness of the regex domain, on rebar's curated suite,
next to `regex`, RE2, PCRE2 and the rest.

It speaks rebar's KLV protocol directly (no dependency on rebar's own crates), so it plugs into a rebar
checkout without building rebar. The runner reads a benchmark as KLV on stdin, takes the model as `argv[1]`
(`count` / `count-spans` / `count-captures` / `grep` / `grep-captures` / `compile`), and prints each timing
sample to stdout as `<nanoseconds>,<count>`. `--version` reports the crate version.

## Scope

`real-regex` is single-pattern and its `str` API needs a UTF-8 haystack, so multi-pattern (RegexSet-style)
and non-UTF-8 benchmarks are reported as an error — rebar records those as unsupported for this engine, the
honest result. The `unicode` and `case-insensitive` KLV flags map to `RegexBuilder::unicode` /
`case_insensitive`. Bounded lookarounds — REAL's differentiator — are a superset the curated suite does not
exercise (no other engine has them); they are measured in the crate's own criterion bench (BENCHMARKS §E.4).

## Replaying the curated suite locally

```sh
# 1. Build the runner.
cargo build --release --manifest-path bindings/rust/rebar/Cargo.toml

# 2. In a rebar checkout, register REAL in benchmarks/engines.toml, e.g.:
#      [[engine]]
#      name = "real"
#      cwd = "<path>/real-regex/bindings/rust/rebar"
#      [engine.version]
#      bin = "target/release/real-regex-rebar"
#      args = ["--version"]
#      [engine.run]
#      bin = "target/release/real-regex-rebar"
#      # rebar appends the model as argv[1] and streams the KLV benchmark on stdin.
#
# 3. Run a slice of the curated suite against it:
#      rebar measure -e '^real$' -f '<filter>' | rebar cmp
```

Upstreaming this as a first-class rebar engine (a PR to BurntSushi/rebar) is deferred until after the current
push — the contact/PR rule. Until then it runs against a local rebar checkout.
