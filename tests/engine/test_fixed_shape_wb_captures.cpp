// Regression: fill_fixed_saves (the fixed_shape fast path's capture filler, pike.hpp) hardcoded
// its scan start at pc 1, assuming that is always the body's own first byte/klass/save -- true
// only when there is no leading \b/\B wrap. With one, pc 1 is the assert_position itself, which
// matches neither the byte/klass nor the save arm of the scan loop, so it hit the loop's `else
// { break; }` on the FIRST instruction and silently filled zero capture slots. Found live by
// differential fuzzing: `rb"\B(\w){2}+"` on `b"C1c_BB11BB"` lost group(1) entirely (spans stayed
// correct -- fixed_shape's own span-finding does not go through this function). Confirmed to be
// a PRE-EXISTING bug, unrelated to D1/D1-perf's possessive quantifiers despite the fuzzer's own
// repro using one: plain GREEDY `\B(\w){2}` (no possessive quantifier at all, predating D1
// entirely) reproduces identically -- verified before attributing the bug to any specific route.
#include <optional>
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

namespace {

  struct case_t {
    const char*                wrap;   // "", "\\b", or "\\B"
    const char*                quant;  // "*+", "++", "{2}+", "{2,4}+"
    std::size_t                start;
    std::size_t                end;
    std::optional<std::string> group1; // nullopt = group unset
  };

  void run_matrix(real::flags extra_flags)
  {
    // Oracle-verified against Python 3.14.6's re on both `"C1c_BB11BB"` (str) and `b"C1c_BB11BB"`
    // (bytes) -- identical spans/groups in both modes, confirming the bug (and the fix) apply the
    // same way regardless of byte- vs text-mode. A LOCAL (not namespace-scope) array: case_t holds
    // a std::optional<std::string>, whose construction can throw -- a static-storage-duration
    // instance would let that exception escape before main() with no way to catch it.
    const std::array<case_t, 12> cases {{
      {.wrap = "", .quant = "*+", .start = 0, .end = 10, .group1 = "B"},
      {.wrap = "", .quant = "++", .start = 0, .end = 10, .group1 = "B"},
      {.wrap = "", .quant = "{2}+", .start = 0, .end = 2, .group1 = "1"},
      {.wrap = "", .quant = "{2,4}+", .start = 0, .end = 4, .group1 = "_"},
      {.wrap = R"(\b)", .quant = "*+", .start = 0, .end = 10, .group1 = "B"},
      {.wrap = R"(\b)", .quant = "++", .start = 0, .end = 10, .group1 = "B"},
      {.wrap = R"(\b)", .quant = "{2}+", .start = 0, .end = 2, .group1 = "1"},
      {.wrap = R"(\b)", .quant = "{2,4}+", .start = 0, .end = 4, .group1 = "_"},
      {.wrap = R"(\B)", .quant = "*+", .start = 1, .end = 10, .group1 = "B"},
      {.wrap = R"(\B)", .quant = "++", .start = 1, .end = 10, .group1 = "B"},
      {.wrap = R"(\B)", .quant = "{2}+", .start = 1, .end = 3, .group1 = "c"},
      {.wrap = R"(\B)", .quant = "{2,4}+", .start = 1, .end = 5, .group1 = "B"},
    }};
    for (const auto& c : cases) {
      const std::string pattern = std::string(c.wrap) + R"((\w))" + c.quant;
      const std::string subject = "C1c_BB11BB";
      const real::regex re(pattern, extra_flags);
      const auto        m = re.search(subject);
      EXPECT(m.matched());
      if (!m.matched()) {
        continue;
      }
      EXPECT_EQ(m.start(), c.start);
      EXPECT_EQ(m.end(), c.end);
      if (c.group1.has_value()) {
        EXPECT(m.start(1) != real::npos);
        EXPECT_EQ(std::string(m[1]), *c.group1);
      }
      else {
        EXPECT(m.start(1) == real::npos);
      }
    }
  }
} // namespace

TEST(fixed_shape_wb_captures_matrix_str_mode)
{
  run_matrix(real::flags::none);
}

TEST(fixed_shape_wb_captures_matrix_bytes_mode)
{
  run_matrix(real::flags::bytes);
}

TEST(fixed_shape_wb_captures_exact_original_repro)
{
  // The exact minimized repro from the fuzz battle, pinned verbatim.
  const std::string subject = "C1c_BB11BB";
  const real::regex re_bytes(R"(\B(\w){2}+)", real::flags::bytes);
  const auto        m = re_bytes.search(subject);
  EXPECT(m.matched());
  EXPECT_EQ(m.start(), 1U);
  EXPECT_EQ(m.end(), 3U);
  EXPECT_EQ(std::string(m[1]), std::string("c"));
}
