# Corpus provenance and licences

Vendored verbatim from public test suites, SHA-256 pinned. Do not edit these files; re-vendor
from the origin and update the digest if a refresh is needed.

## rust-regex — `rust/*.toml`

Source: <https://github.com/rust-lang/regex> (`testdata/`). Licence: **MIT OR Apache-2.0**,
Copyright the Rust Project Developers. Semantics: leftmost-first (== Python `re`).

| File | SHA-256 |
| --- | --- |
| `rust/anchored.toml` | `7a1b5cd81deed2099796a451bf764a3f9bd21f0d60c0fa46accd3a35666866f2` |
| `rust/bytes.toml` | `1d84179165fd25f3b94bd2bfbeb43fc8a162041f7bf98b717e0f85cef7fb652b` |
| `rust/crlf.toml` | `d19cf22756434d145dd20946c00af01c102a556a252070405c3c8294129d9ece` |
| `rust/empty.toml` | `738dbe92fbd8971385a1cf3affb0e956e5b692c858b9b48439d718f10801c08e` |
| `rust/flags.toml` | `9a7e001808195c84f2a7d3e18bc0a82c7386e60f03a616e99af00c3f7f2c3fd4` |
| `rust/iter.toml` | `6875460302974a5b3073a7304a865c45aba9653c54afea2c4d26e1ea248a81f7` |
| `rust/misc.toml` | `32c9591655c6fb118dfefcb4de49a04820a63cb960533dfc2538cdaabf4f4047` |
| `rust/multiline.toml` | `eb07cf5427e6ddbcf61f4cc64c2d74ff41b5ef75ef857959651b20196f3cd157` |
| `rust/no-unicode.toml` | `d209da04506900fd5f69e48170cddaad0702355ac6176c3a75ab3ff96974457c` |
| `rust/regression.toml` | `6006ef4fcfbfd7155ce5ce8b8427904f7261c5549396f20cb065c0294733686d` |
| `rust/substring.toml` | `48122d9f3477ed81f95e3ad42c06e9bb25f849b66994601a75ceae0693b81866` |
| `rust/unicode.toml` | `7e4b013039b0cdd85fa73f32d15d096182fe901643d4e40c0910087a736cd46d` |
| `rust/utf8.toml` | `2eabce0582bcacb2073e08bbe7ca413f096d14d06e917b107949691e24f84b20` |
| `rust/word-boundary-special.toml` | `7d0ea2f796478d1ca2a6954430cb1cfbd04031a182f8611cb50a7c73e443ce33` |
| `rust/word-boundary.toml` | `51bc1c498ab825420340a2dd3e6623de4054937ba6d5020ff8cd14b1c1e45271` |

## Fowler / AT&T — `fowler/*.dat`

Source: <https://github.com/golang/go> (`src/regexp/testdata/`), the Go project's copy of the
AT&T Labs (Glenn Fowler) POSIX regression tests. Licence: **BSD-3-Clause**, Copyright the Go
Authors. Semantics: POSIX (leftmost-longest) — the origin; disagreements with our leftmost-first
oracle are `out_of_contract`, not failures.

| File | SHA-256 |
| --- | --- |
| `fowler/basic.dat` | `4fafe8de5fc2462cedbb9af53314fb4bb1d5418142970ed0ace69210b18fe4a4` |
| `fowler/nullsubexpr.dat` | `b3efa203749c79e51e4e9cb8cfd378bb8139bcdefc49d97f71e80218db2fc39b` |
| `fowler/repetition.dat` | `d0cd1f1e7774d01138002ee80262f3cb5fe7ef44d1c7f5c538d3953b409a5eca` |
