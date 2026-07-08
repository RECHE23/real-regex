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
    {.pat = R"(a\d+X)",             .text = "aX a1X aa12X 12X a123Xzz X aXX"},                      // literal X, prefix a\d+ (overlap)
    {.pat = R"(\d{4}-\d{2})",       .text = "9999-99 12-3456-78 no 0000-00"},
    {.pat = R"(GET /\w+ )",         .text = "GET /a POST /b GET /home x GET / y GET /ok "},
    {.pat = R"(((.))a)",             .text = "aaaab aabaa baaaa aXaya"},                            // the exhaustive-flagged weak/adjacent literal
    {.pat = R"((a?)a)",              .text = "aaa baa aXa aaaa"},                                   // adjacent literal, optional prefix
    {.pat = R"(.+@)",                .text = "aaaa@x bb@ c@d@e no @"},                              // greedy prefix: reverse must not over-bound
    {.pat = R"(id=[0-9a-f]{8})",     .text = "id=abc12345 id=00000000 x id=deadbeef no id= id=zz"}, // offset-0 literal
    // IL-FUSION cases (fiche IL-FUSION): the whole pattern is fixed_shape, so these take the
    // arithmetic-verify fast path (match_byte_klass_run, no reverse/forward DFA) instead of the
    // reverse-DFA + confirm_at route the cases above still exercise. `\d` is deliberately NOT used here:
    // the Unicode shorthand compiles to klass_cp, which fixed_shape (and so il_fused_eligible) always
    // excludes -- explicit byte classes are what the fused path (and the benchmarked "date" case,
    // bench_engines.cpp) actually compiles to.
    {.pat = R"([0-9]{4}-[0-9]{2}-[0-9]{2})", .text = "x2026-07-04y 2026-13-99 no-date 99-99-99 z2026-12-25w"},  // hits abutting non-digit/dash noise
    {.pat = R"(([0-9]{4})-([0-9]{2})-([0-9]{2}))", .text = "log 2026-07-04 x 2026-99-99 bad-date 2099-01-01!"}, // grouped: exercises fill_fixed_saves on the fused path
    {.pat = R"([0-9]{4}-[0-9]{2}-[0-9]{2})", .text = "2026-07-04"},                                             // literal hit at h == prefix width exactly (h - prefix_w == 0, the tightest bounds-guard case)
    {.pat = R"([0-9]{4}-[0-9]{2}-[0-9]{2})", .text = "9-04"},                                                   // prefix cannot fit before the hit at all (h < prefix_w) -- must decline, not underflow
    {.pat = R"([0-9]{10}-[0-9]{10}-[0-9]{10})", .text = "x0123456789-0123456789-01234567890y bad"},             // total width 32 (== il_fused_max_width, the boundary): still fused
    {.pat = R"([0-9]{15}-[0-9]{15}-[0-9]{15})", .text = "012345678901234-012345678901234-012345678901234"},     // total width 47 (> il_fused_max_width): stays on the pre-fusion route, must still match correctly
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

TEST(inner_literal_fusion_group_captures_match_core)
{
  // The span-only differential above does not read sub-groups; the fused path fills them via
  // fill_fixed_saves (constant offsets from the match start, no re-match) instead of one-pass
  // extraction, so pin the GROUP VALUES themselves, routed vs core, on a fixed-shape pattern with an
  // inner literal and multiple captures. Explicit byte classes, not \d (klass_cp -- not fixed_shape).
  const real::regex re   {R"(([0-9]{4})-([0-9]{2})-([0-9]{2}))"};
  const std::string text {"log 2026-07-04 x bad-date 2099-12-25 end"};

  real::detail::inner_literal_guard_disabled() = true;
  real::detail::inner_literal_route_disabled() = true;
  const auto core   {re.find_all(text)};
  real::detail::inner_literal_route_disabled() = false;
  const auto routed {re.find_all(text)};
  real::detail::inner_literal_route_disabled() = false;
  real::detail::inner_literal_guard_disabled() = false;

  EXPECT_EQ(core.size(), 2U);
  EXPECT_EQ(routed.size(), core.size());
  for (std::size_t i = 0; i < core.size(); ++i) {
    EXPECT_EQ(routed[i][0], core[i][0]); // whole match
    EXPECT_EQ(routed[i][1], core[i][1]); // year
    EXPECT_EQ(routed[i][2], core[i][2]); // month
    EXPECT_EQ(routed[i][3], core[i][3]); // day
  }
  EXPECT_EQ(core[0][1], std::string_view("2026"));
  EXPECT_EQ(core[0][2], std::string_view("07"));
  EXPECT_EQ(core[0][3], std::string_view("04"));
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

TEST(inner_literal_fusion_d3_acid_stays_linear)
{
  // The same D3 acid, but on a fixed_shape pattern (explicit byte classes, not \d) so it actually
  // exercises the FUSED verify's own linearity, not just the pre-fusion reverse-DFA route's: the fused
  // path deliberately does not advance min_pre_start on a failed candidate (match_byte_klass_run
  // reports pass/fail only), so this pins that the omission does not reopen the quadratic risk the
  // guard exists for -- linear because each candidate is a hard-bounded O(il_fused_max_width) check,
  // not because the guard caught it.
  real::detail::inner_literal_route_disabled() = false; // undo the previous test's trailing state
  std::string text;
  for (int i = 0; i < 50000; ++i) {
    text += "x-"; // "-" every two bytes, never preceded by four digits -> every fused verify fails
  }
  const real::regex re {R"([0-9]{4}-[0-9]{2})"};
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
