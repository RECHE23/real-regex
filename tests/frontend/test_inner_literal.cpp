//! IL.0a fixtures: the inner-literal extraction is a pure function on the AST. This pins the classification
//! of the duel + flagship patterns (extracted vs declined) and the soundness acid — a pattern with a path
//! that bypasses the literal must NOT extract.
#include <sciforge/test/framework.hpp>

#include <real/frontend/inner_literal.hpp>
#include <real/frontend/compiler.hpp>

#include <cstring>
#include <string_view>

namespace {
  real::detail::inner_literal extract(std::string_view pattern,
                                      real::flags      f = real::flags::none)
  {
    return real::detail::extract_inner_literal(real::detail::parse(pattern, f));
  }

  bool is_lit(const real::detail::inner_literal& il,
              std::string_view                   expected)
  {
    return il.len == expected.size()
           && std::memcmp(il.bytes.data(), expected.data(), il.len) == 0;
  }
}

TEST(inner_literal_extracted)
{
  // The required literal every match must contain — the maximal guaranteed byte run, most-selective wins.
  EXPECT(is_lit(extract(R"((\w+)@(\w+))"), "@"));        // the @ between two word runs
  EXPECT(is_lit(extract(R"(\d{4}-\d{2}-\d{2})"), "-"));  // candidate-first: the - has no fixed offset
  EXPECT(is_lit(extract(R"(key=(\w+))"), "key="));       // a 4-byte run beats its single rare byte
  EXPECT(is_lit(extract(R"((\w+)_(\w+))"), "_"));        // extracted even though the ident arc is separate
  EXPECT(is_lit(extract(R"((foo)bar)"), "foobar"));      // descends the group; the run spans the boundary
}

TEST(inner_literal_declined_soundness)
{
  // The acid: a bypassing path means the literal is NOT required -> no extraction.
  EXPECT(!extract(R"((a)?@b)").found());   // (a)? optional -> conservative decline
  EXPECT(!extract(R"(foo|@bar)").found()); // alternation -> no common required literal
  EXPECT(!extract(R"(x*@?y)").found());    // @? optional -> @ is not guaranteed
}

TEST(inner_literal_declined_routed)
{
  EXPECT(!extract(R"(^foo)").found());       // anchored -> handled without a scan
  EXPECT(!extract(R"((?=foo)bar)").found()); // lookaround -> VM-routed
  EXPECT(!extract(R"(a|b)").found());        // top-level alternation
  EXPECT(!extract(R"(\w+)").found());        // no literal run at all
}

TEST(inner_literal_bytes_mode)
{
  // Literals are bytes, so the extraction works identically in bytes mode.
  EXPECT(is_lit(extract(R"((\w+)@(\w+))", real::flags::bytes), "@"));
}

TEST(inner_literal_prefix_boundary)
{
  // IL.1 boundary: how many top-level children precede the literal (the sub-pattern the prefix-reverse matches
  // from a candidate back to the match start). 0 = literal at head (reverse is identity, start = candidate).
  EXPECT(extract(R"(\d{4}-\d{2}-\d{2})").prefix_child_count == 1); // prefix = \d{4}
  EXPECT(extract(R"((\w+)@(\w+))").prefix_child_count == 1);       // prefix = (\w+)
  EXPECT(extract(R"(key=(\w+))").prefix_child_count == 0);         // literal at head -> empty prefix
  EXPECT(extract(R"(@\w+)").prefix_child_count == 0);              // head literal: reverse must return s = candidate
  EXPECT(extract(R"((foo)bar)").prefix_child_count == -1);         // literal opens inside a group -> no clean boundary
}

TEST(inner_literal_d1a_peels_top_level_wb)
{
  // D1a: leading/trailing `\b`/`\B` no longer decline extraction — they set prefix_skip + wb hints
  // so the reverse-prefix excludes the assert (byte-DFA) while confirm re-checks boundaries.
  const auto both {extract(R"(\b\w+@\w+\b)")};
  EXPECT(both.found());
  EXPECT(is_lit(both, "@"));
  EXPECT_EQ(both.prefix_child_count, 1); // body-local: just `\w+` before `@`
  EXPECT_EQ(both.prefix_skip, 1);        // peeled lead `\b`
  EXPECT_EQ(static_cast<int>(both.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(both.wb_trail), 1);

  const auto lead_only {extract(R"(\b\w+@\w+)")};
  EXPECT(lead_only.found());
  EXPECT_EQ(lead_only.prefix_skip, 1);
  EXPECT_EQ(static_cast<int>(lead_only.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(lead_only.wb_trail), 0);

  // Mid-body wb still declines (unsound for a required-literal + reverse-prefix split).
  EXPECT(!extract(R"(\w+\b@\w+)").found());
  // Non-wb anchors still decline.
  EXPECT(!extract(R"(^\w+@\w+)").found());
}


TEST(inner_literal_stored_in_hints)
{
  // IL.2 foundation: the extraction is recorded in pattern_hints at compile (the search path's input).
  const real::detail::dynamic_program p1 {real::detail::compile(real::detail::parse(R"(\d{4}-\d{2})", real::flags::none), real::flags::none)};
  EXPECT(p1.hints.inner_literal_len == 1 && p1.hints.inner_literal[0] == static_cast<std::uint8_t>('-'));
  EXPECT(p1.hints.inner_literal_prefix == 1);

  const real::detail::dynamic_program p2 {real::detail::compile(real::detail::parse(R"((\w+)@(\w+))", real::flags::none), real::flags::none)};
  EXPECT(p2.hints.inner_literal_len == 1 && p2.hints.inner_literal[0] == static_cast<std::uint8_t>('@'));
  EXPECT(p2.hints.inner_literal_prefix == 1);

  // No required literal -> empty hint.
  const real::detail::dynamic_program p3 {real::detail::compile(real::detail::parse(R"(\w+)", real::flags::none), real::flags::none)};
  EXPECT(p3.hints.inner_literal_len == 0);
}

TEST(inner_literal_prefix_program_stored)
{
  // IL.2 Stage A: at (dynamic) compile, the prefix sub-program is compiled and stored for the reverse
  // start-finder — populated when there is a top-level prefix, empty for a head literal or no literal.
  const real::detail::dynamic_program p1 {real::detail::compile(real::detail::parse(R"(\\d{4}-\\d{2})", real::flags::none), real::flags::none)};
  EXPECT(!p1.prefix_code.empty()); // prefix = \d{4}

  const real::detail::dynamic_program p2 {real::detail::compile(real::detail::parse(R"(@\\w+)", real::flags::none), real::flags::none)};
  EXPECT(p2.prefix_code.empty());  // head literal (count 0): reverse is the identity, no prefix program

  const real::detail::dynamic_program p3 {real::detail::compile(real::detail::parse(R"(\\w+)", real::flags::none), real::flags::none)};
  EXPECT(p3.prefix_code.empty());  // no required literal
}
