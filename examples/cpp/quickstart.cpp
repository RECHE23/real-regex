// quickstart.cpp — the landing page's C++ quickstart tab (docs/site/_templates/landing.html,
// doc-site P1c), compiled and run here so the code the page shows is exactly the code that's
// tested, never merely illustrative (doc-site P1b-A gate-snippet). A pair of region-boundary
// comments further down bounds the block conf.py's `_inject_quickstart` (html-page-context
// hook) reads and Pygments-highlights onto the page at build time (NOTE: keep this sentence
// from ever spelling out that pair of comments literally — the hook's own region search is a
// first-occurrence substring search, the same start-after/end-before contract Sphinx
// `literalinclude` used before P1c, and a mention up here would shadow the real one below) — do
// not edit the marked lines without also checking the landing's C++ tab still matches
// byte-for-byte (rebuild with `make docs-site` and diff).
//
// The two #include lines inside main() are REDUNDANT (both headers are already included above,
// so their include guards make the second inclusion a no-op) — kept only so the marked region
// reproduces the landing's displayed text exactly. `line` is predeclared just above the region,
// exactly as the prose above the tab implies ("...or drop into std::regex's place").
#include <real/real.hpp>
#include <real/compat/std/regex.hpp>

#include <iostream>
#include <string>

int main()
{
  const std::string line = "key=value";   // referenced by the compat block below

  // [quickstart]
  // The pattern lives in the type — parsed & compiled at compile time.
  #include <real/real.hpp>
  constexpr real::static_regex<R"((\d{1,3})(?:\.(\d{1,3})){3})"> ipv4;
  static_assert(ipv4.match("192.168.0.1"));

  // …or drop into std::regex's place — same API, linear-time engine.
  #include <real/compat/std/regex.hpp>
  real::compat::smatch m;
  real::compat::regex_search(line, m, real::compat::regex{R"((\w+)=(\S+))"});
  // [/quickstart]

  const bool ok = ipv4.match("192.168.0.1") && m.size() > 0 && m[1].str() == "key" && m[2].str() == "value";
  std::cout << "quickstart: ipv4 static_assert held; regex_search matched=" << std::boolalpha
            << (m.size() > 0) << " (group1=" << (m.size() > 0 ? m[1].str() : std::string{}) << ")\n";
  return ok ? 0 : 1;
}
