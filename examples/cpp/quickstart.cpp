// The landing page's C++ quickstart tab, compiled and run here so the code the page
// shows is exactly the code that's tested, never merely illustrative. A pair of region-boundary
// comments further down bounds the block conf.py's `_inject_quickstart` (html-page-context
// hook) reads and Pygments-highlights onto the page at build time (NOTE: keep this sentence
// from ever spelling out that pair of comments literally — the hook's own region search is a
// first-occurrence substring search, and a mention up here would shadow the real one below) — do
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
#include <string_view>

using namespace std::string_view_literals;

int main()
{
  // [quickstart]
  // The pattern lives in the type — parsed & compiled at compile time.
  #include <real/real.hpp>
  constexpr real::static_regex<R"((\w+)@(\w+))"> email;
  static_assert(email.search("info@example.com")[2] == "example");
  // [/quickstart]

  const bool ok = email.search("info@example.com")[2] == "example"sv;
  std::cout << "quickstart: email static_assert held; group 2 = " << email.search("info@example.com")[2] << "\n";
  return ok ? 0 : 1;
}
