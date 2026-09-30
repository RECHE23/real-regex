//! In text mode a match never starts on a UTF-8 continuation byte, save at the region's start. A pattern that can
//! open on one -- a raw `\x80`-`\xBF` at its lead -- is where the literal, alternation and loop routes would find
//! the byte inside a code point; these pin the rule on every public walk and against the VM alone.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"
#include "real/regex_set.hpp"

namespace {

  using span_list = std::vector<std::pair<std::size_t, std::size_t>>;

  span_list spans_of(const real::regex& re,
                     std::string_view   s)
  {
    span_list out;
    for (const auto& m : re.find_iter(s)) {
      out.emplace_back(m.start(), m.end());
    }
    return out;
  }

  bool continuation(std::string_view s,
                    std::size_t      at)
  {
    return at < s.size() && (static_cast<std::uint8_t>(s[at]) & 0xC0U) == 0x80U;
  }

  //! Leads that open on a continuation byte, one per route that used to take them.
  constexpr std::string_view leads[] {
    R"(\xA9)",                                                           // exact literal
    R"(\xA9 )",                                                          // exact literal, two bytes
    R"(\xA9|zz)",                                                        // alternation
    R"(\xA9z|\xA8z|\xA7z|\xA6z|\xA5z|\xA4z|\xA3z|\xA2z|\xA1z|\xA0z|zz)", // alternation past the small set
    R"(\xA9+)",                                                          // class loop
    R"((?:\xA9|\xA8) )",                                                 // fixed shape pair
    R"(\xA9\b)",                                                         // literal with a trailing boundary
    R"(\b\xA9)",                                                         // literal with a leading boundary
    R"(\xA9{2})",                                                        // repeated literal
    R"((?m)^\xA9)",                                                      // a lead assertion the lazy DFA represents, before the byte
    R"((?a)\B\xA9)",                                                     // an ASCII boundary, which the lazy DFA represents too
    R"((?a)\b\xA9*z)"}; // the reverse walk would extend the start back over the bytes


  //! Subjects mixing two- to four-byte code points whose tails hold the lead's bytes, and raw stray ones.
  std::vector<std::string> subjects()
  {
    const char* const        units[] {"\xC3\xA9", "\xC2\xA9", " zz ", "a", "\xE2\x82\xAC", "\xA9", "z", "\xF0\x9F\x98\x80", "\xC2\xA8 ", "\n"};
    std::vector<std::string> out;
    std::uint32_t            state   {0x2545F491U};
    for (int n {0}; n < 40; ++n) {
      std::string s;
      const auto  len {static_cast<std::size_t>(8 + (n * 37))};
      while (s.size() < len) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        s     += units[state % std::size(units)];
      }
      out.push_back(std::move(s));
    }
    return out;
  }
} // namespace

// No walk starts a match inside a code point where it did not resume, and every walk answers as the VM alone.
TEST(text_mode_never_starts_a_match_on_a_continuation_byte)
{
  std::size_t checked {0};
  std::size_t found   {0};
  for (const std::string_view lead : leads) {
    const real::regex re {lead};
    for (const std::string& s : subjects()) {
      const span_list got     {spans_of(re, s)};
      std::size_t     resumed {0}; // each step of the walk searches from the previous end: its region's start
      for (const auto& [start, end] : got) {
        const bool inside {start != resumed && continuation(s, start)};
        if (inside) {
          std::printf("/%.*s/ starts at %zu, inside a code point\n", static_cast<int>(lead.size()), lead.data(), start);
        }
        EXPECT(!inside);
        resumed = end;
      }
      real::detail::lazy_dfa_route_disabled()          = true;
      real::detail::bounded_backtrack_route_disabled() = true;
      const real::regex vm   {lead};
      const span_list   want {spans_of(vm, s)};
      real::detail::lazy_dfa_route_disabled()          = false;
      real::detail::bounded_backtrack_route_disabled() = false;
      EXPECT(got == want);
      EXPECT_EQ(re.count_matches(s), want.size());
      const auto first {re.search(s)};
      EXPECT_EQ(first.matched(), !want.empty());
      if (first.matched() && !want.empty()) {
        EXPECT_EQ(first.start(), want.front().first);
      }
      found += got.size();
      ++checked;
    }
  }
  EXPECT_EQ(checked, std::size(leads) * subjects().size());
  EXPECT(found > 0U); // the stray raw bytes and the region starts do match
}

// The region's own start is exempt: a subject that begins with a continuation byte matches there.
TEST(text_mode_matches_a_continuation_byte_at_the_region_start)
{
  const real::regex re {R"(\xA9)"};
  EXPECT_EQ(re.search("\xA9 tail").start(), 0U);
  EXPECT(!re.search("\xC2\xA9").matched());
  EXPECT_EQ(re.search("\xC2\xA9", 1).start(), 1U);
  EXPECT_EQ(re.count_matches("a\xA9"), 0U);
}

// Bytes mode keeps every byte a start, and keeps its routes.
TEST(bytes_mode_starts_a_match_on_any_byte)
{
  const real::regex re {R"(\xA9)", real::flags::bytes};
  EXPECT_EQ(re.count_matches("\xC3\xA9 \xC2\xA9"), 2U);
  EXPECT(real::detail::dynamic_storage::compile(R"(\xA9)", real::flags::bytes).program.hints.exact_literal_len > 0U);
}

// The compile-time storage reads the same program: it answers as the dynamic regex.
TEST(static_regex_never_starts_a_match_on_a_continuation_byte)
{
  static constexpr real::static_regex<R"(\xA9|zz)">  fixed;
  const real::regex                                  dynamic {R"(\xA9|zz)"};
  for (const std::string& s : subjects()) {
    EXPECT_EQ(fixed.count_matches(s), dynamic.count_matches(s));
  }
}

// A set's member answers as the member alone: a code point's tail is not a match of `\xA9`.
TEST(regex_set_never_starts_a_match_on_a_continuation_byte)
{
  const real::regex_set set {R"(\xA9)", "zz"};
  std::string           prose;
  while (prose.size() < 4000U) {
    prose += "a\xC3\xA9 b ";
  }
  for (const std::string_view s : {std::string_view {"a\xC3\xA9 b"}, std::string_view {prose}}) {
    const std::vector<bool> hit {set.matches(s)};
    EXPECT(!hit[0]);
    EXPECT(!hit[1]);
  }
  EXPECT(set.matches("\xA9 at the start")[0]);
}

// Under allow_raw_byte a `\C` lead starts where RE2's does, inside a code point, on every walk and subject size.
TEST(raw_byte_lead_may_start_inside_a_code_point)
{
  const real::regex re {R"(\Cx)", real::flags::allow_raw_byte};
  const std::string s  {"\xC3\xA9x \xC3\xA9x"};
  EXPECT(spans_of(re, s) == (span_list {{1, 3}, {5, 7}}));
  std::string big;
  while (big.size() < 4000U) {
    big += s;
  }
  EXPECT_EQ(re.count_matches(big), 2U * (big.size() / s.size()));
  EXPECT(real::detail::dynamic_storage::compile(R"(\C+)", real::flags::allow_raw_byte).program.hints.greedy_class_loop >= 0);
  const real::regex          dfa_shape {R"(\C[a-z]+\d)", real::flags::allow_raw_byte}; // the lazy DFA's route past its floor
  constexpr std::string_view unit      {"\xC3\xA9" "ab1 "};
  std::string                prose;
  while (prose.size() < 4000U) {
    prose += unit;
  }
  EXPECT_EQ(dfa_shape.search(prose).start(), 1U);
  EXPECT_EQ(dfa_shape.count_matches(prose), prose.size() / unit.size());
}
