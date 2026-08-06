// An unbounded quantifier over a non-ASCII code point had no fast route, and the fix must not change
// one answer while taking one.
//
// `é` in text mode parses to its UTF-8 bytes wrapped in a concat, so `é+` repeated a two-byte
// SEQUENCE -- a shape no route recognises. The route counters named it: 18 896 `lazy_dfa_anchored`
// dispatches over a 180 KB corpus, one per match, where `[à-ÿ]+` (a RANGE, which stays a code-point
// class) needed none at all. The witness that settled the cause was `[éa]+`: one ASCII member breaks
// the common length, the byte-wise form declines, and the row is fast again. Both the literal and the
// class now become a one-member code-point class under such a quantifier: 5.6 -> 1.77 ns/B and
// `[éàèùç]+` 8.4 -> 1.92, identical on two independent builds.
//
// What these tests defend is that the promotion is EXACT. It fires only where the body is one code
// point, so `(?:éé)+` and `(?:ab)+` must be refused -- promoting them would change what the
// quantifier repeats -- and a code-point class must never match an invalid or overlong encoding.
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  //! Counts occurrences of \p op in the pattern's compiled program.
  std::size_t count_op(const std::string&   pattern,
                       real::detail::opcode op,
                       real::flags          f = real::flags::none)
  {
    const real::regex                             rx    {pattern, f};
    std::size_t                                   n     {0};
    const std::vector<real::detail::program_view> views {rx.raw_program()};
    for (const real::detail::program_view& view : views) {
      for (const real::detail::instr& in : view.code) {
        if (in.op == op) {
          ++n;
        }
      }
    }
    return n;
  }

  //! The whole-match span of the leftmost match, or npos/npos when there is none.
  std::pair<std::size_t, std::size_t> span(const std::string& pattern,
                                           const std::string& subject)
  {
    const real::regex re {pattern};
    const auto        m  {re.search(subject)};
    if (!m.matched()) {
      return {real::npos, real::npos};
    }
    return {m.start(), m.end()};
  }
} // namespace

// A quantifier over a multi-byte literal repeats the whole CODE POINT, not its last byte. Three
// encoding lengths, so the promotion is exercised at 2, 3 and 4 bytes.
TEST(an_unbounded_quantifier_repeats_the_whole_codepoint)
{
  EXPECT_EQ(span("é+", "ééé"), std::make_pair(std::size_t {0}, std::size_t {6})); // 3 x 2 bytes
  EXPECT_EQ(span("あ+", "あああ"), std::make_pair(std::size_t {0}, std::size_t {9})); // 3 x 3 bytes
  EXPECT_EQ(span("𝔘+", "𝔘𝔘"), std::make_pair(std::size_t {0}, std::size_t {8}));  // 2 x 4 bytes

  EXPECT_EQ(span("é+", "aéb"), std::make_pair(std::size_t {1}, std::size_t {3}));
  EXPECT_EQ(span("é+x", "éééx"), std::make_pair(std::size_t {0}, std::size_t {7}));
  EXPECT_EQ(span("é+", "a").first, real::npos);

  const real::regex re {"é+"};
  EXPECT_EQ(re.count_matches("ééé"), 1U);
  EXPECT_EQ(re.count_matches("éxéxé"), 3U);
}

// The class spellings agree with the literal, and with the range that already had the route.
TEST(the_class_spellings_agree_with_the_literal)
{
  EXPECT_EQ(span("[é]+", "ééé"), std::make_pair(std::size_t {0}, std::size_t {6}));
  EXPECT_EQ(span("(?:é)+", "ééé"), std::make_pair(std::size_t {0}, std::size_t {6}));
  EXPECT_EQ(span("[éàèùç]+", "éàè"), std::make_pair(std::size_t {0}, std::size_t {6}));
  EXPECT_EQ(span("[à-ÿ]+", "éàè"), std::make_pair(std::size_t {0}, std::size_t {6}));
  // The mixed class was never promotable (members differ in length) and is unchanged.
  EXPECT_EQ(span("[éa]+", "aéa"), std::make_pair(std::size_t {0}, std::size_t {4}));
}

// A body of MORE than one code point must be refused: the strict decode consumes fewer bytes than
// the chain holds, so the promotion declines rather than repeating the wrong thing.
TEST(a_multi_codepoint_body_is_refused)
{
  // Five é: `(?:éé)+` takes two whole repetitions (8 bytes) and leaves the fifth.
  EXPECT_EQ(span("(?:éé)+", "ééééé"), std::make_pair(std::size_t {0}, std::size_t {8}));
  EXPECT_EQ(span("(?:ab)+", "ababab"), std::make_pair(std::size_t {0}, std::size_t {6}));
  EXPECT_EQ(span("(?:éa)+", "éaéa"), std::make_pair(std::size_t {0}, std::size_t {6}));
  // A single é followed by a distinct atom: `+` binds to the é alone.
  EXPECT_EQ(span("éa+", "éaa"), std::make_pair(std::size_t {0}, std::size_t {4}));
}

// The other quantifier shapes keep their semantics: lazy takes one code point at a time, `{n,}`
// counts code points, `*` still matches empty, and a BOUNDED form is not promoted at all.
TEST(the_other_quantifier_shapes_are_unchanged)
{
  EXPECT_EQ(span("é+?", "ééé"), std::make_pair(std::size_t {0}, std::size_t {2}));
  EXPECT_EQ(real::regex {"é+?"}.count_matches("ééé"), 3U);
  EXPECT_EQ(span("é{2,}", "ééé"), std::make_pair(std::size_t {0}, std::size_t {6}));
  EXPECT_EQ(span("é{2}", "ééé"), std::make_pair(std::size_t {0}, std::size_t {4}));
  EXPECT_EQ(span("é*", "xx"), std::make_pair(std::size_t {0}, std::size_t {0}));
  EXPECT_EQ(span("é*", "éé"), std::make_pair(std::size_t {0}, std::size_t {4}));
}

// A code-point class never matches an invalid or overlong encoding — the guarantee the byte-wise form
// also had, and the one a promotion could most easily have broken. "C3 C3 A9" holds exactly one valid
// two-byte sequence, at offset 1.
TEST(an_invalid_encoding_is_still_never_matched)
{
  const std::string bogus {"\xC3\xC3\xA9"};
  const real::regex re    {"é+"};
  EXPECT_EQ(re.count_matches(bogus), 1U);
  EXPECT_EQ(re.search(bogus).start(), 1U);

  const std::string overlong {"\xC0\xA9"}; // an overlong encoding of ')' — never a code point
  EXPECT_EQ(re.count_matches(overlong), 0U);
}

// Bytes mode is a byte NFA with no code points in it, so the promotion must not apply there: the two
// bytes of `é` are two independent bytes, and `+` binds to the second alone.
TEST(bytes_mode_is_left_to_its_byte_semantics)
{
  const real::regex re   {"\xC3\xA9+", real::flags::bytes};
  EXPECT_EQ(re.count_matches("ééé"), 3U);              // one per C3 A9 pair
  const real::regex tail {"\xA9+", real::flags::bytes};
  EXPECT_EQ(tail.count_matches("ééé"), 3U);            // the continuation byte alone is matchable
}

// A BARE non-ASCII class keeps the byte-wise fixed-width form, which is what routes it as an exact
// literal — the promotion is scoped to quantifier bodies and must not have taken that away.
TEST(a_bare_class_keeps_its_fixed_width_form)
{
  EXPECT_EQ(span("[é]", "aéb"), std::make_pair(std::size_t {1}, std::size_t {3}));
  EXPECT_EQ(span("(?i)café", "un CAFÉ ici"), std::make_pair(std::size_t {3}, std::size_t {8}));
  EXPECT_EQ(real::regex {"(?i)é"}.count_matches("éÉé"), 3U);
}

// THE TEST THAT FAILS WITHOUT THE FIX. Everything above pins semantics, and semantics do not change --
// this is a routing change, so what has to be pinned is the compiled SHAPE. A promoted body is one
// `klass_cp` and no literal `byte`; a refused one keeps its bytes. Asserted directly rather than
// through the aggregate golden hash, which can only say that something moved.
TEST(the_promoted_body_compiles_to_one_codepoint_class)
{
  using real::detail::opcode;

  // Promoted: the literal, the one-member class, and the several-member class the byte-wise
  // fixed-width form would otherwise have taken.
  EXPECT_EQ(count_op("é+", opcode::klass_cp), 1U);
  EXPECT_EQ(count_op("é+", opcode::byte), 0U);
  EXPECT_EQ(count_op("[é]+", opcode::klass_cp), 1U);
  EXPECT_EQ(count_op("[é]+", opcode::byte), 0U);
  EXPECT_EQ(count_op("[éàèùç]+", opcode::klass_cp), 1U);
  EXPECT_EQ(count_op("[éàèùç]+", opcode::byte), 0U);
  EXPECT_EQ(count_op("あ+", opcode::klass_cp), 1U);
  EXPECT_EQ(count_op("あ+", opcode::byte), 0U);

  // NOT promoted, and each for its own reason: a bare class keeps the fixed-width byte form that
  // routes it as an exact literal; a bounded quantifier is outside the promotion; a body of more than
  // one code point is refused by the strict decode.
  EXPECT(count_op("[é]", opcode::byte) > 0U);
  EXPECT(count_op("é{2}", opcode::byte) > 0U);
  EXPECT(count_op("(?:éé)+", opcode::byte) > 0U);
  EXPECT(count_op("(?:ab)+", opcode::byte) > 0U);
  // Bytes mode has no code points at all.
  EXPECT_EQ(count_op("\xC3\xA9+", opcode::klass_cp, real::flags::bytes), 0U);
}
