// Regression: the B-1 optimization (resolve_class_wb_hints, prefilter.hpp) drops a leading \b
// check as "provably redundant" for a maximal greedy/possessive class run, reasoning that a
// maximal run can only ever START where the preceding byte is non-word. That argument silently
// assumes "no preceding byte" means the true text start -- it does not: search(text, pos, endpos)
// with pos > 0 can plant a candidate run exactly at the window's own edge, with a REAL word byte
// sitting just outside the window at text[pos - 1]. B-1 then wrongly treats that edge as a \b.
// Verified against CPython 3.14.6: pos acts as a virtual text-start for a LEADING \b/\B, but
// (asymmetrically) endpos correctly still acts as a virtual text-end for a TRAILING \b/\B -- only
// the leading side needed a fix. Found live while running the differential fuzzer's own 5x100k
// closure battle (first seed, first 100k run): `pattern='\b\w+' text='a0b1a' flags=24` diverged
// from CPython at search(text, pos, endpos)/match(text, pos, endpos) for pos in [1,4]. Pre-existing
// (reproduces on an unmodified v2026.7.36 tip via plain greedy \b\w+, no possessive quantifier
// involved) and spans three runners: run_class_loop, run_cp_class_loop, and the possessive own
// run_possessive_loop_generic -- fixed via a new pattern_hints::wb_lead_maximal_run flag, set at
// all four resolve_class_wb_hints call sites, consumed as a runtime guard in each runner.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace {

  struct window_case_t {
    std::size_t                pos;
    std::size_t                endpos;
    std::optional<std::string> expect; // nullopt = no match
  };

  void run_search_matrix(std::string_view                       pattern,
                         std::string_view                       text,
                         const std::vector<window_case_t>&      cases,
                         real::flags                            extra_flags = real::flags::none)
  {
    const real::regex re(std::string(pattern), extra_flags);
    for (const auto& c : cases) {
      const auto m = re.search(text, c.pos, c.endpos);
      if (!c.expect.has_value()) {
        EXPECT(!m.matched());
        continue;
      }
      EXPECT(m.matched());
      if (m.matched()) {
        EXPECT_EQ(std::string(m[0]), *c.expect);
      }
    }
  }

  void run_match_matrix(std::string_view                       pattern,
                        std::string_view                       text,
                        const std::vector<window_case_t>&      cases)
  {
    const real::regex re {std::string(pattern)};
    for (const auto& c : cases) {
      const auto m = re.match(text, c.pos, c.endpos);
      if (!c.expect.has_value()) {
        EXPECT(!m.matched());
        continue;
      }
      EXPECT(m.matched());
      if (m.matched()) {
        EXPECT_EQ(std::string(m[0]), *c.expect);
      }
    }
  }
} // namespace

// --- bare greedy class loop (run_class_loop) ---------------------------------------------------

TEST(wb_windowed_search_leading_b_greedy_class_loop)
{
  // Oracle: Python 3.14.6, re.compile(r"\b\w+").search("a0b1a", pos, 5).
  run_search_matrix(R"(\b\w+)", "a0b1a"sv,
                    {{.pos  = 0, .endpos = 5, .expect = "a0b1a"},
                      {.pos = 1, .endpos = 5, .expect = std::nullopt},
                      {.pos = 2, .endpos = 5, .expect = std::nullopt},
                      {.pos = 3, .endpos = 5, .expect = std::nullopt},
                      {.pos = 4, .endpos = 5, .expect = std::nullopt}});
}

TEST(wb_windowed_search_leading_capital_b_greedy_class_loop_unaffected)
{
  // \B is never dropped by B-1 (wb_redundant_for_full_word bails on lead==2/trail==2), so this
  // must be unaffected by the fix -- pinned as a control.
  run_search_matrix(R"(\B\w+)", "a0b1a"sv,
                    {{.pos  = 0, .endpos = 5, .expect = "0b1a"},
                      {.pos = 1, .endpos = 5, .expect = "0b1a"},
                      {.pos = 2, .endpos = 5, .expect = "b1a"},
                      {.pos = 3, .endpos = 5, .expect = "1a"},
                      {.pos = 4, .endpos = 5, .expect = "a"}});
}

TEST(wb_windowed_search_leading_b_greedy_class_loop_bytes_mode)
{
  run_search_matrix(R"(\b\w+)", "a0b1a"sv,
                    {{.pos  = 0, .endpos = 5, .expect = "a0b1a"},
                      {.pos = 1, .endpos = 5, .expect = std::nullopt},
                      {.pos = 3, .endpos = 5, .expect = std::nullopt}},
                    real::flags::bytes);
}

TEST(wb_windowed_match_leading_b_greedy_class_loop)
{
  // match() exercises the mode::prefix single-shot branch, distinct from search()'s retry loop.
  run_match_matrix(R"(\b\w+)", "a0b1a"sv,
                   {{.pos  = 0, .endpos = 5, .expect = "a0b1a"},
                     {.pos = 1, .endpos = 5, .expect = std::nullopt},
                     {.pos = 4, .endpos = 5, .expect = std::nullopt}});
}

// --- cp_class (Unicode \p{L}) loop (run_cp_class_loop) ------------------------------------------

TEST(wb_windowed_search_leading_b_cp_class_loop)
{
  // "aébèa" (UTF-8): a, C3 A9, b, C3 A8, a -- 7 bytes, all \p{L}, no internal boundary.
  const std::string text {"a\xc3\xa9" "b\xc3\xa8" "a"};
  run_search_matrix(R"(\b\p{L}+)", text,
                    {{.pos  = 0, .endpos = text.size(), .expect = text},
                      {.pos = 1, .endpos = text.size(), .expect = std::nullopt},
                      {.pos = 3, .endpos = text.size(), .expect = std::nullopt},
                      {.pos = text.size(), .endpos = text.size(), .expect = std::nullopt}});
}

// --- possessive loop (run_possessive_loop_generic) --------------------------------------

TEST(wb_windowed_search_leading_b_possessive_plus)
{
  // Oracle: Python 3.14.6, re.compile(r"\b\w++").search("a0b1a", pos, 5) -- identical outcomes to
  // the greedy \b\w+ case above (single-class atomic loop, nothing to backtrack into either way).
  run_search_matrix(R"(\b\w++)", "a0b1a"sv,
                    {{.pos  = 0, .endpos = 5, .expect = "a0b1a"},
                      {.pos = 1, .endpos = 5, .expect = std::nullopt},
                      {.pos = 2, .endpos = 5, .expect = std::nullopt},
                      {.pos = 3, .endpos = 5, .expect = std::nullopt},
                      {.pos = 4, .endpos = 5, .expect = std::nullopt}});
}

TEST(wb_windowed_match_leading_b_possessive_plus)
{
  run_match_matrix(R"(\b\w++)", "a0b1a"sv,
                   {{.pos  = 0, .endpos = 5, .expect = "a0b1a"},
                     {.pos = 1, .endpos = 5, .expect = std::nullopt},
                     {.pos = 4, .endpos = 5, .expect = std::nullopt}});
  run_match_matrix(R"(\B\w++)", "a0b1a"sv,
                   {{.pos  = 0, .endpos = 5, .expect = std::nullopt},
                     {.pos = 1, .endpos = 5, .expect = "0b1a"},
                     {.pos = 4, .endpos = 5, .expect = "a"}});
}

TEST(wb_windowed_search_leading_b_possessive_star_nonword_suffix)
{
  // min=0 (star) possessive with a non-word suffix so the class run can't swallow it. Oracle:
  // Python 3.14.6, re.compile(r"\b\w*+,").search("a0b1,", pos, 5) -- at pos in [1,4] the run scans
  // forward past the false window-edge boundary and lands on the REAL boundary at index 4 (word
  // '1' -> non-word ',').
  run_search_matrix(R"(\b\w*+,)", "a0b1,"sv,
                    {{.pos  = 0, .endpos = 5, .expect = "a0b1,"},
                      {.pos = 1, .endpos = 5, .expect = ","},
                      {.pos = 2, .endpos = 5, .expect = ","},
                      {.pos = 3, .endpos = 5, .expect = ","},
                      {.pos = 4, .endpos = 5, .expect = ","}});
}

TEST(wb_windowed_search_leading_capital_b_possessive_star_unaffected)
{
  run_search_matrix(R"(\B\w*+,)", "a0b1,"sv,
                    {{.pos  = 0, .endpos = 5, .expect = "0b1,"},
                      {.pos = 1, .endpos = 5, .expect = "0b1,"},
                      {.pos = 2, .endpos = 5, .expect = "b1,"},
                      {.pos = 3, .endpos = 5, .expect = "1,"},
                      {.pos = 4, .endpos = 5, .expect = std::nullopt}});
}

TEST(wb_windowed_search_possessive_route_toggle_agrees)
{
  // Route-auto (possessive fast path armed) must agree with forced-general on
  // every window, since Bug C independently affected BOTH routes before the fix -- a regression
  // here would mean the two routes silently diverged again.
  const real::regex      re(R"(\b\w*+,)");
  const std::string_view text {"a0b1,"};
  for (std::size_t pos {0}; pos <= text.size(); ++pos) {
    real::detail::possessive_fastpath_disabled() = false;
    const auto        auto_route   {re.search(text, pos, text.size())};
    const bool        auto_matched {auto_route.matched()};
    const std::size_t auto_start   {auto_matched ? auto_route.start() : 0};
    const std::size_t auto_end     {auto_matched ? auto_route.end() : 0};
    real::detail::possessive_fastpath_disabled() = true;
    const auto forced_general      {re.search(text, pos, text.size())};
    real::detail::possessive_fastpath_disabled() = false;
    EXPECT_EQ(auto_matched, forced_general.matched());
    if (auto_matched && forced_general.matched()) {
      EXPECT_EQ(auto_start, forced_general.start());
      EXPECT_EQ(auto_end, forced_general.end());
    }
  }
}

// --- trailing side: endpos must remain UNCHANGED by this fix (asymmetric on purpose) ------------

TEST(wb_windowed_search_trailing_b_unaffected_greedy)
{
  // Oracle: Python 3.14.6 -- endpos correctly still acts as a virtual text-end for a TRAILING \b,
  // so \w+\b / \w++\b must keep matching exactly up to endpos. Pinned as a control so a future
  // "symmetric fix" attempt on the trailing side cannot land silently.
  run_search_matrix(R"(\w+\b)", "a0b1a"sv,
                    {{.pos  = 0, .endpos = 3, .expect = "a0b"},
                      {.pos = 0, .endpos = 2, .expect = "a0"}});
  run_search_matrix(R"(\w++\b)", "a0b1a"sv,
                    {{.pos  = 0, .endpos = 3, .expect = "a0b"},
                      {.pos = 0, .endpos = 2, .expect = "a0"}});
}
