# Unreleased — The release workflow's publishing action carries its security fix

## Changed

- **Release only: PyPI publishing runs `pypa/gh-action-pypi-publish` 1.14.3**, which no longer bundles the emSign
  root CA in the trust store Sigstore reads; the wheels and the engine are unchanged.
- **Development only: the long compat fuzzing runs hold on a counted repeat of quantifiers.** The fuzzer skips the
  `std::regex` comparison where a group with unbounded quantifiers sits under `{m}`, `{m,M}` or `{m,}` and a
  backtracker would try more than 10^5 ways to spread the subject over it: `(\w*w*){81}` over 14 bytes took libstdc++
  0.85 s a search (about 10^20 ways), REAL 0.1 ms with the same match.
