// The landing page's C++ quickstart tab, compiled and run here so the code the page shows is
// exactly the code that's tested, never merely illustrative. Editing the marked region changes
// the landing: rebuild with `make docs-site` and diff the C++ tab.
//
// Three constraints on that region, all load-bearing:
//   - It is compiled AS ITS OWN TRANSLATION UNIT (tools/check_quickstart_displayed.py), so a
//     paste of those lines alone must build. Nothing in it may lean on a header included further
//     up this file — hence its own #include, which the guard above makes a no-op *here* only.
//   - It sits at NAMESPACE scope in that unit: no statement may need an enclosing function,
//     which is why the answer is asserted rather than printed.
//   - conf.py's `_extract_region` finds it with `str.index`, i.e. the FIRST occurrence of each
//     boundary comment, so no line above may spell either of them out.
#include <real/real.hpp>

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
