//! IL.2: the routed==core differential. The inner-literal search path must give byte-identical results to the
//! core Pike search — every match span, on a corpus plus adversarial cases (multi-occurrence, orphan hits, the
//! overlap where a confirm-match need not contain the current literal hit). The route toggle proves it within
//! one binary; the D3 acid pins linearity.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp> // inner_literal_route_disabled
#include <real/real.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
  std::vector<std::pair<std::size_t, std::size_t>> spans(std::string_view pat,
                                                         std::string_view text)
  {
    const real::regex                                re {pat};
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (const auto& m : re.find_iter(text)) {
      out.emplace_back(m.start(0), m.end(0));
    }
    return out;
  }
}

TEST(inner_literal_routed_equals_core)
{
  struct testcase { std::string_view pat; std::string_view text; };
  const testcase cases[] {
    {.pat = R"(\d{4}-\d{2}-\d{2})", .text = "log 2026-07-04 x 2026-12-25 e no-date 99-99-99 here"},
    {.pat = R"((\w+)@(\w+))",       .text = "a@b x@y c@d word noat foo@bar @ trailing@"},
    {.pat = R"(key=(\w+))",         .text = "key=val key=x notkey= key=z end key="},
    {.pat = R"(\w+@)",              .text = "ab@ cd@ @ orphan@ ef@ trailing no"},
    {.pat = R"(@\w+)",              .text = "@a @bc @ @def head @"},
    {.pat = R"(a\d+X)",             .text = "aX a1X aa12X 12X a123Xzz X aXX"}, // literal X, prefix a\d+ (overlap)
    {.pat = R"(\d{4}-\d{2})",       .text = "9999-99 12-3456-78 no 0000-00"},
    {.pat = R"(GET /\w+ )",         .text = "GET /a POST /b GET /home x GET / y GET /ok "},
    {.pat = R"(((.))a)",             .text = "aaaab aabaa baaaa aXaya"},       // the exhaustive-flagged weak/adjacent literal
    {.pat = R"((a?)a)",              .text = "aaa baa aXa aaaa"},              // adjacent literal, optional prefix
    {.pat = R"(.+@)",                .text = "aaaa@x bb@ c@d@e no @"},         // greedy prefix: reverse must not over-bound
  };
  // These inputs are tiny (< the small-haystack guard's floor), so the guard would send the routed run back to
  // the core and the comparison would be trivially true. Disable it: the point is to exercise the route.
  real::detail::inner_literal_guard_disabled() = true;
  for (const testcase& tc : cases) {
    real::detail::inner_literal_route_disabled() = true;
    const auto core   {spans(tc.pat, tc.text)};
    real::detail::inner_literal_route_disabled() = false;
    const auto routed {spans(tc.pat, tc.text)};
    EXPECT(core == routed);
  }
  real::detail::inner_literal_route_disabled() = false;
  real::detail::inner_literal_guard_disabled() = false;
}


TEST(inner_literal_d3_acid_stays_linear)
{
  // D3: the literal every few bytes, every confirm failing. The reverse bound + the sticky abandon must keep
  // it linear — a quadratic loop over ~100 KB would take many seconds (minutes under sanitizers); linear is ms.
  std::string text;
  for (int i = 0; i < 50000; ++i) {
    text += "x-"; // "-" every two bytes, never preceded by four digits -> every confirm fails
  }
  const real::regex re {R"(\d{4}-\d{2})"};
  const auto        t0 {std::chrono::steady_clock::now()};
  std::size_t       n  {0};
  for (const auto& m : re.find_iter(text)) {
    (void) m;
    ++n;
  }
  const auto ms {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()};
  EXPECT(n == 0);
  EXPECT(ms < 5000); // generous even under sanitizers; a quadratic scan would not finish in time
  real::detail::inner_literal_route_disabled() = true;
}
