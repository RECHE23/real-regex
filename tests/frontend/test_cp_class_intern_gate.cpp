// Every consumer of a code-point class requires its ranges ordered and disjoint, and until now that
// requirement was written only where it is CONSUMED: two match-time paths split at U+07FF and both
// read the order as meaning -- the U+0080..U+07FF bitmap is filled by a loop that stops at the first
// range past `cp_page_max`, and everything above is answered by a binary search. Nothing asked for it
// where classes are PRODUCED, and nothing asked at the point they all pass through.
//
// The cost of that gap is measured, not hypothetical: a producer collecting ranges in source order
// satisfied the requirement only for inputs already sorted, and an alternation of single code points
// whose first exceeded U+07FF and outranked a later one lost members on both sides of the boundary at
// once -- `🥨|é` matched neither of its own branches, across every binding. The program's SHAPE was
// identical to the correct one; the only visible symptom was an answer.
//
// So these tests are about the GATE, not about that pattern. Since every producer now normalises, no
// pattern can hand an unordered list to the interner any more, which is exactly why the witness has to
// FABRICATE one: the input is built here and handed to `intern_cp_class` directly. That reaches into
// `real::detail` deliberately -- `compiler` promises nothing outward, and a gate whose omission cannot
// be seen is not a gate.
#include <cstdint>
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  using real::detail::class_def;
  using real::detail::code_range;

  //! A class with non-ASCII ranges only, in exactly the order given — no normalisation on the way in.
  class_def cp_only(std::vector<code_range> ranges)
  {
    class_def cd;
    cd.ranges = std::move(ranges);
    return cd;
  }

  //! Interns a well-formed class, callable during constant evaluation.
  constexpr bool interns_during_constant_evaluation()
  {
    real::detail::dynamic_program prog;
    class_def                     cd;
    cd.ranges = {{0xE9U, 0xE9U}, {0x1F968U, 0x1F968U}};
    return real::detail::compiler::intern_cp_class(prog, cd) == 0U && prog.cp_classes.size() == 1U;
  }

  // The real risk in guarding an invariant with a THROW is constexpr-ness: `static_regex` compiles its
  // program during constant evaluation, so the interner runs there too, and a throw on a path a
  // well-formed class takes would turn every such regex into a hard compile error. Checked here at
  // build time rather than asserted in prose.
  //
  // The other direction is NOT witnessed in this file, and cannot be: an unordered class in a constant
  // expression is a COMPILE error, so no runtime assertion can observe it. Measured out of tree —
  // `static_assert` on an interner call with `{{0x1F968, 0x1F968}, {0xE9, 0xE9}}` fails with the
  // compiler pointing at this gate's throw — and recorded here as a gap rather than implied to hold.
  static_assert(interns_during_constant_evaluation(),
                "the normalisation gate must not make a well-formed code-point class non-constant");
} // namespace

// The predicate is the testable unit; the interner only asks it. Each refusal below is a distinct way
// a list can be wrong, and each acceptance is a shape a producer legitimately hands over.
TEST(the_normalisation_predicate_separates_every_way_a_range_list_can_be_wrong)
{
  using real::detail::cp_ranges_are_normalised;

  // Accepted: nothing to order, one range, an ascending disjoint pair, the whole non-ASCII space.
  EXPECT(cp_ranges_are_normalised({}));
  EXPECT(cp_ranges_are_normalised({{0xE9U, 0xE9U}}));
  EXPECT(cp_ranges_are_normalised({{0xE9U, 0xE9U}, {0x1F968U, 0x1F968U}}));
  EXPECT(cp_ranges_are_normalised({{0x80U, 0x10FFFFU}}));

  // Accepted, and deliberately so: two ranges that merely TOUCH are redundant, not wrong. Neither
  // consumer reacts to a split that `coalesce_ranges` would have merged — it costs one comparison and
  // no answer — so the predicate demands order and disjointness, never minimality.
  EXPECT(cp_ranges_are_normalised({{0x100U, 0x1FFU}, {0x200U, 0x300U}}));

  // Refused: descending. The shape that actually reached the matchers.
  EXPECT(!cp_ranges_are_normalised({{0x1F968U, 0x1F968U}, {0xE9U, 0xE9U}}));
  EXPECT(!cp_ranges_are_normalised({{0xE9U, 0xE9U}, {0x1F968U, 0x1F968U}, {0x20ACU, 0x20ACU}}));

  // Refused: overlapping, which breaks the binary search's premise as surely as disorder does.
  EXPECT(!cp_ranges_are_normalised({{0x100U, 0x200U}, {0x180U, 0x300U}}));
  EXPECT(!cp_ranges_are_normalised({{0x100U, 0x300U}, {0x200U, 0x250U}}));

  // Refused: a range naming no code point at all, which no amount of ordering makes meaningful.
  EXPECT(!cp_ranges_are_normalised({{0x200U, 0x100U}}));
  EXPECT(!cp_ranges_are_normalised({{0xE9U, 0xE9U}, {0x300U, 0x200U}}));
}

// The gate itself: the interner must ASK, and must say which invariant broke. Asserted on the message
// and not on the throw alone — the class-count ceiling next to it throws `regex_error` too, and a
// witness that accepts any `regex_error` cannot tell a normalisation refusal from an unrelated one.
TEST(interning_an_unordered_class_is_refused_and_says_which_invariant_broke)
{
  real::detail::dynamic_program prog;
  bool                          threw {false};
  try {
    real::detail::compiler::intern_cp_class(prog, cp_only({{0x1F968U, 0x1F968U}, {0xE9U, 0xE9U}}));
    EXPECT(false); // reached only when the gate is not asked
  }
  catch (const real::regex_error& ex) {
    threw = true;
    EXPECT(std::string {ex.what()}.find("code-point class ranges are not normalised") !=
           std::string::npos);
  }
  EXPECT(threw);
  EXPECT(prog.cp_classes.empty()); // refused BEFORE anything was recorded

  // An overlap and an inverted range go the same way, so the gate is the predicate's whole answer and
  // not a check on one of its arms.
  EXPECT_THROWS(real::detail::compiler::intern_cp_class(
                  prog, cp_only({{0x100U, 0x300U}, {0x200U, 0x250U}})), real::regex_error);
  EXPECT_THROWS(real::detail::compiler::intern_cp_class(
                  prog, cp_only({{0x200U, 0x100U}})), real::regex_error);
}

// And the gate refuses nothing a producer legitimately builds: the same SET, normalised the way every
// producer normalises it, interns and dedups exactly as before.
TEST(interning_a_normalised_class_succeeds_and_still_dedups)
{
  real::detail::dynamic_program prog;
  const std::vector<code_range> raw {{0x1F968U, 0x1F968U}, {0xE9U, 0xE9U}};

  const std::uint16_t first         {
    real::detail::compiler::intern_cp_class(prog, cp_only(real::detail::coalesce_ranges(raw)))};
  EXPECT_EQ(first, 0U);
  EXPECT_EQ(prog.cp_classes.size(), 1U);

  // The same content interns to the same index — the gate sits ahead of the dedup, not in place of it.
  const std::uint16_t again {
    real::detail::compiler::intern_cp_class(prog, cp_only(real::detail::coalesce_ranges(raw)))};
  EXPECT_EQ(again, first);
  EXPECT_EQ(prog.cp_classes.size(), 1U);

  // An empty range list is a pure-ASCII class and has nothing to order; it must not be caught.
  const std::uint16_t ascii_only {real::detail::compiler::intern_cp_class(prog, cp_only({}))};
  EXPECT_EQ(ascii_only, 1U);
  EXPECT_EQ(prog.cp_classes.size(), 2U);

  // The routine the `static_assert` above constant-evaluates, RUN: the gate must answer alike in both
  // evaluation modes, and a constexpr function reached only from a `static_assert` is never executed,
  // so it would otherwise sit in the coverage report as a function nothing calls.
  EXPECT(interns_during_constant_evaluation());
}

// The gate must not have been paid for by undoing the fix that motivated it: the fused alternation
// still normalises, so the pattern from the report still answers on both of its branches.
TEST(the_fusion_still_normalises_so_the_reported_pattern_still_answers)
{
  EXPECT(real::regex {"🥨|é"}.search("🥨").matched());
  EXPECT(real::regex {"🥨|é"}.search("é").matched());
  EXPECT_EQ(real::regex {"🥨|€|é"}.count_matches("🥨€é"), 3U);
}
