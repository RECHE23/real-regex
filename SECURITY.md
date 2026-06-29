# Security Policy

## Supported versions

REAL ships under CalVer (`YYYY.M.PATCH`). Only the **most recent release** receives
fixes; older releases do not.

| Version | Supported |
| --- | --- |
| latest release | ✅ |
| older releases | ❌ |

## Reporting a vulnerability

Please report security issues privately through GitHub's
**[Report a vulnerability](https://github.com/RECHE23/real-regex/security/advisories/new)**
(private vulnerability reporting). Do **not** open a public issue for a security report.

### A ReDoS bypass *is* a vulnerability

REAL's core guarantee is **linear-time matching**: no pattern and input should drive it
into super-linear — let alone exponential — time. If you find a pattern and an input for
which REAL is **not** linear (a bypass of the ReDoS-safety guarantee), treat it as a
security vulnerability and report it through the channel above, with the pattern, the
input (or a generator for it), and the observed scaling. That is exactly the kind of
report this policy exists to receive.
