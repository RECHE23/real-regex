// An alternation whose every branch is one literal byte is a byte class written the long way, and the
// long way had no route: the split chain matches no shape recognizer while the class does. Fused, the
// program is not merely equivalent to the hand-written class — it is IDENTICAL to it (5 instructions,
// one `klass`, one `split`, where the chain took 11 with three `byte`s and three `split`s), and the
// timings agree to the third decimal: `(?:a|b|c)+` 0.640 ns/B against `[abc]+`'s 0.640, from 2.31
// before. `(?:e|o|u)+` over English prose goes 7.05 -> 1.105, a 6.2x.
//
// The fusion is EXACT rather than approximate, and the argument is short: every branch consumes
// exactly one byte and none captures, so leftmost-first preference among them has no observable
// effect — the span is the same whichever branch a backtracker would have picked. What these tests
// defend is the refusal list, because each entry is a way the argument stops holding.
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  //! Instruction-shape summary of a compiled pattern: (total, klass, byte, split).
  struct shape
  {
    std::size_t total {}, klass {}, byte {}, split {};

    bool operator==(const shape&) const = default;
  };

  shape shape_of(const std::string& pattern,
                 real::flags        f = real::flags::none)
  {
    const real::regex                             rx    {pattern, f};
    const std::vector<real::detail::program_view> views {rx.raw_program()};
    shape                                         s;
    for (const real::detail::program_view& v : views) {
      for (const real::detail::instr& in : v.code) {
        ++s.total;
        s.klass += in.op == real::detail::opcode::klass ? 1U : 0U;
        s.byte  += in.op == real::detail::opcode::byte ? 1U : 0U;
        s.split += in.op == real::detail::opcode::split ? 1U : 0U;
      }
    }
    return s;
  }
} // namespace

// THE TEST THAT FAILS WITHOUT THE FUSION. Equivalence to the class is not asserted by hand — the two
// programs are compared, and they must be the same one.
TEST(a_single_byte_alternation_compiles_to_the_class_it_is)
{
  EXPECT(shape_of("(?:a|b|c)+") == shape_of("[abc]+"));
  EXPECT(shape_of("a|b|c") == shape_of("[abc]"));
  EXPECT(shape_of("(?:x|y)") == shape_of("[xy]"));
  // …and the fused form really is the class shape, not merely equal to something else.
  EXPECT_EQ(shape_of("(?:a|b|c)+").klass, 1U);
  EXPECT_EQ(shape_of("(?:a|b|c)+").byte, 0U);
  EXPECT_EQ(shape_of("(?:a|b|c)+").split, 1U);
}

// Every refusal is a way the exactness argument stops holding, and each is checked by SHAPE, so a
// future widening cannot quietly swallow one.
TEST(the_refusals_each_have_their_own_reason)
{
  // An EMPTY branch: `a|` matches the empty string, which no byte class can express.
  EXPECT(shape_of("a|") != shape_of("[a]"));
  EXPECT(real::regex {"a|"}.search("zzz").matched());   // still matches empty
  EXPECT_EQ(real::regex {"a|"}.search("zzz").end(), 0U);

  // A branch of more than one byte is a sequence, not a member.
  EXPECT(shape_of("(?:a|bc)+").byte > 0U);
  EXPECT_EQ(real::regex {"(?:a|bc)+"}.search("bca").end(), 3U);

  // A branch that is itself a class.
  EXPECT(shape_of("(?:a|[b-d])+").byte > 0U || shape_of("(?:a|[b-d])+").klass > 1U);
  EXPECT_EQ(real::regex {"(?:a|[b-d])+"}.search("abcd").end(), 4U);

  // A non-ASCII branch is a concat of UTF-8 bytes, so it is a sequence too.
  EXPECT_EQ(real::regex {"(?:a|é)+"}.search("aé").end(), 3U);

  // One branch is not an alternation to fuse; that path is unchanged.
  EXPECT_EQ(real::regex {"(?:a)+"}.search("aaa").end(), 3U);
}

// The fused class must answer exactly as the alternation did, including where a backtracker's branch
// order could have been observable — it is not, because every branch is one byte and none captures.
TEST(the_fused_class_answers_as_the_alternation_did)
{
  const std::string subject {"the quick brown fox jumps over the lazy dog"};
  for (const char* pair : {"(?:a|b|c)+", "[abc]+"}) {
    const real::regex re {pair};
    EXPECT_EQ(re.count_matches(subject), real::regex {"[abc]+"}.count_matches(subject));
  }
  EXPECT_EQ(real::regex {"(?:e|o|u)+"}.count_matches(subject),
            real::regex {"[eou]+"}.count_matches(subject));

  // Order does not matter, and neither do duplicates.
  EXPECT_EQ(real::regex {"(?:c|a|b)+"}.search("abcabc").end(), 6U);
  EXPECT_EQ(real::regex {"(?:a|a|b)+"}.search("aab").end(), 3U);

  // A capture around the alternation still captures the byte the alternation matched.
  const auto m {real::regex {"(a|b)x"}.search("zbx")};
  EXPECT(m.matched());
  EXPECT_EQ(std::string {m[1]}, std::string {"b"});
}

// Flags reach the branches before the fusion sees them: under icase the parser has already promoted a
// cased literal to a class, so those branches are not `byte` nodes and the fusion declines — which is
// why the folded form keeps its own path rather than being silently narrowed to one case.
TEST(scoped_flags_and_bytes_mode_are_unaffected)
{
  EXPECT_EQ(real::regex {"(?i)(?:a|b)+"}.count_matches("aAbB"), 1U);
  EXPECT_EQ(real::regex {"(?i)(?:a|b)+"}.search("aAbB").end(), 4U);

  // Bytes mode is a byte NFA already; fusing single bytes there is the same transformation.
  EXPECT(shape_of("(?:a|b|c)+", real::flags::bytes) == shape_of("[abc]+", real::flags::bytes));
  EXPECT_EQ(real::regex("(?:a|b|c)+", real::flags::bytes).count_matches("abcabc"), 1U);
}

// The same fusion, one encoding wider: a branch that is one non-ASCII CODE POINT is a member too.
// `(?:é|à|è)+` measured 7.47 ns/B against `[éàè]+`'s 1.83 -- the same 4x-shaped gap the ASCII form
// had, from the same cause, because a non-ASCII literal is a concat of UTF-8 bytes and so was not a
// `byte` branch. It now asks the predicate the parser and emit_unbounded_body already ask.
//
// The property to hold onto is not the speed but the CONSISTENCY: `(?:é|à|è)` and `[éàè]` are the
// same language, so they must compile to the same program in every context. Emitting the fused set
// directly as a code-point class was measurably faster on the bare form (1.85 against 4.42) and was
// REFUSED for exactly that reason -- one spelling routing differently from the other is the drift,
// not the win. The class emission is shared instead, and the quantifier body asks the same fusion
// with its own emission, which is where the 4x actually belongs.
TEST(a_single_codepoint_alternation_is_the_class_it_is)
{
  // Same program as the hand-written class, quantified and bare, mixed and pure.
  EXPECT(shape_of("(?:é|à|è)+") == shape_of("[éàè]+"));
  EXPECT(shape_of("(?:é|à|è)") == shape_of("[éàè]"));
  EXPECT(shape_of("(?:é|a)+") == shape_of("[éa]+"));
  EXPECT(shape_of("(?:あ|い)+") == shape_of("[あい]+"));
  EXPECT(shape_of("(?:𝔘|𝔙)+") == shape_of("[𝔘𝔙]+"));

  // …and the answers agree, including where branch order could have mattered.
  const std::string subject {"café résumé naïve façade élève"};
  EXPECT_EQ(real::regex {"(?:é|à|è)+"}.count_matches(subject),
            real::regex {"[éàè]+"}.count_matches(subject));
  EXPECT_EQ(real::regex {"(?:è|é|à)+"}.count_matches(subject),
            real::regex {"[éàè]+"}.count_matches(subject));
  EXPECT_EQ(real::regex {"(?:é|a)+"}.count_matches(subject),
            real::regex {"[éa]+"}.count_matches(subject));

  // The refusals are unchanged by the widening: a multi-CHARACTER branch is still a sequence.
  EXPECT(shape_of("(?:é|àè)+") != shape_of("[éàè]+"));
  EXPECT_EQ(real::regex {"(?:é|àè)+"}.search("àèé").end(), 6U);
  EXPECT(shape_of("é|") != shape_of("[é]"));
  EXPECT_EQ(real::regex {"é|"}.search("zz").end(), 0U);

  // Bytes mode has no code points: there the branches are raw bytes and only those fuse.
  EXPECT_EQ(real::regex("(?:a|b)+", real::flags::bytes).count_matches("abab"), 1U);
}

// The fused set must be NORMALISED, not merely collected: the matchers that consume a code-point class
// require its ranges ascending and non-overlapping, in two independent places that split at U+07FF.
// Below the boundary the U+0080..U+07FF bitmap is filled by a loop that STOPS at the first range past
// it; above, membership is a binary search. A list left in branch order therefore loses members on
// BOTH sides at once, which is why the two assertions below are about the SAME pattern failing twice.
//
// Asserted on answers, and it has to be: `shape_of` compares opcode counts, and the fused form emits
// exactly the hand-written class's opcodes whatever the order -- the difference lives entirely in the
// interned ranges, where shape cannot see it. Every order-independence claim above this test happens
// to sit below U+0800, where neither consumer can react, so none of them reached this property.
//
// The merge half of the normalisation is deliberately unwitnessed here: a list of duplicates is
// already sorted, so no such pattern can go red, and a green would pin nothing.
TEST(the_fused_class_is_normalised_however_the_branches_were_ordered)
{
  // Descending, and spanning the boundary: neither branch answers when the order survives.
  EXPECT(real::regex {"🥨|é"}.search("🥨").matched());
  EXPECT(real::regex {"🥨|é"}.search("é").matched());
  EXPECT_EQ(real::regex {"🥨|é"}.count_matches("un 🥨 et un é"), 2U);

  // Nothing astral is required -- two ordinary multi-byte characters in descending order are enough,
  // and the smaller one need not be Latin.
  EXPECT(real::regex {"€|é"}.search("€").matched());
  EXPECT(real::regex {"€|é"}.search("é").matched());
  EXPECT(real::regex {"あ|€"}.search("あ").matched());
  EXPECT(real::regex {"あ|€"}.search("€").matched());

  // Three branches, worst order, and the same set written ascending must answer identically.
  EXPECT_EQ(real::regex {"🥨|€|é"}.count_matches("🥨€é"), 3U);
  EXPECT_EQ(real::regex {"🥨|€|é"}.count_matches("🥨€é"),
            real::regex {"é|€|🥨"}.count_matches("🥨€é"));

  // The spellings that were already right are untouched: a hand-written class, an ascending
  // alternation, an ASCII second branch (which lands in the bitmap, not the ranges), a pair that
  // stays under U+0800, bytes mode (where the fusion declines), and the folded path.
  EXPECT(real::regex {"[🥨é]"}.search("é").matched());
  EXPECT(real::regex {"é|🥨"}.search("🥨").matched());
  EXPECT(real::regex {"🥨|e"}.search("🥨").matched());
  EXPECT(real::regex {"🥨|e"}.search("e").matched());
  EXPECT(real::regex {"é|à"}.search("é").matched());
  EXPECT(real::regex {"é|à"}.search("à").matched());
  EXPECT(real::regex("🥨|é", real::flags::bytes).search("🥨").matched());
  EXPECT(real::regex("🥨|é", real::flags::bytes).search("é").matched());
  EXPECT(real::regex {"(?i)🥨|é"}.search("🥨").matched());
  EXPECT(real::regex {"(?i)🥨|é"}.search("é").matched());

  // And the fusion still produces the hand-written class's program, which is the promise the
  // normalisation exists to keep rather than to weaken.
  EXPECT(shape_of("🥨|é") == shape_of("[🥨é]"));
  EXPECT(shape_of("(?:🥨|€|é)+") == shape_of("[🥨€é]+"));
}
