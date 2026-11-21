# Unreleased — The release workflow's publishing action carries its security fix

## Changed

- **Release only: PyPI publishing runs `pypa/gh-action-pypi-publish` 1.14.3**, which no longer bundles the emSign
  root CA in the trust store Sigstore reads; the wheels and the engine are unchanged.
- **Development only: the long compat fuzzing runs hold on a counted repeat of quantifiers.** The fuzzer skips the
  `std::regex` comparison where a group with unbounded quantifiers sits under `{m}`, `{m,M}` or `{m,}` and a
  backtracker would try more than 10^5 ways to spread the subject over it: `(\w*w*){81}` over 14 bytes took libstdc++
  0.85 s a search (about 10^20 ways), REAL 0.1 ms with the same match.
- **Development only: CI follows Ubuntu 26.04 and Rust 1.100.** The s390x job runs the suite under qemu-user's
  `qemu-s390x` (26.04 ships no `qemu-user-static`), and the Rust binding passes Rust 1.100's clippy; the gates run
  their tools under `$(PYTHON)`, and SciForge is pinned at v2026.10.7, as NumericAL pins it.
