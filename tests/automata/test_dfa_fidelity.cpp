// real::dfa_faithful — whether a DFA built from a pattern reproduces the pattern's `match()`.
//
// A DFA takes the LONGEST match; `match()` takes the one its priority order prefers. Which patterns keep
// the two equal is not a syntactic property -- a greedy pattern can differ and a lazy one can agree -- so
// every expectation below is checked against the engine rather than asserted from the pattern's shape:
// a `divergent` answer must come with a witness on which the two really differ, and a `faithful` one must
// survive exhaustive comparison over every short input.
#include <array>
#include <cstddef>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/dfa.hpp"

namespace {

  using real::dfa_fidelity_outcome;

  //! `match()`'s length on \p text, or -1 when it does not match.
  long priority_length(const real::regex& re,
                       std::string_view   text)
  {
    const auto m {re.match(text)};
    return m ? static_cast<long>(m.end()) : -1L;
  }

  //! The longest anchored match of \p re on \p text, or -1. The DFA never reports an empty match, so
  //! a nullable pattern's longest is 0 exactly when the DFA finds nothing and `match()` still matches.
  long longest_length(const real::regex& re,
                      std::string_view   text)
  {
    const std::vector<real::regex> one {re};
    const real::dfa                d   {std::span<const real::regex>(one)};
    if (const auto hit {d.match(text)}) {
      return static_cast<long>(hit->length);
    }
    return re.match(text) ? 0L : -1L;
  }

  //! Whether \p text separates `match()` from the longest match — what a witness has to be.
  bool separates(const real::regex& re,
                 std::string_view   text)
  {
    return priority_length(re, text) != longest_length(re, text);
  }

  //! A random pattern over a small alphabet, mixing every construct that orders alternatives.
  std::string random_pattern(std::mt19937& rng,
                             int           depth)
  {
    static constexpr std::array<const char*, 8> atoms {"a", "b", "c", "[ab]", "[^a]", "é", "(?i:a)", "^"};
    const int                                   k     {std::uniform_int_distribution<int>(0, depth <= 0 ? 1 : 9)(rng)};
    if (k <= 1) {
      return atoms[std::uniform_int_distribution<std::size_t>(0, atoms.size() - 1)(rng)];
    }
    const std::string x    {random_pattern(rng, depth - 1)};
    const bool        lazy {(rng() & 1U) != 0U};
    switch (k) {
      case 2:
      case 3: return x + random_pattern(rng, depth - 1);
      case 4:
      case 5: return "(?:" + x + "|" + random_pattern(rng, depth - 1) + ")";
      case 6: return "(?:" + x + ")" + (lazy ? "*?" : "*");
      case 7: return "(?:" + x + ")" + (lazy ? "??" : "?");
      case 8: return "(?:" + x + ")" + (lazy ? "+?" : "+");
      default: return "(?:" + x + ")" + (lazy ? "{0,2}?" : "{1,3}");
    }
  }
} // namespace

// The class is semantic. Each of these differs on an input, and none of the ways it differs is a lazy
// quantifier: an alternative that accepts early outranks one that would accept later.
TEST(dfa_faithful_finds_a_witness_for_every_divergent_shape)
{
  const std::vector<std::string> divergent {
    "a|ab",                             // an earlier branch that is a prefix of a later one
    "in|int",                           // the same, in a keyword list
    "as|assert",                        //
    "(?:ab|a)(?:bc)?",                  // greedy, LONGER branch first, and still divergent
    "(?:a|ab)*",                        // inside a repetition
    R"((?s)<!\[CDATA\[.*?\]\]>)",       // lazy, with a delimiter the text can repeat
    "(?:ab|a)(?:bcdefghijklmnop)?",     // a witness longer than any short probe
  };
  for (const std::string& p : divergent) {
    const real::regex        re {p};
    const real::dfa_fidelity f  {real::dfa_faithful(re)};
    EXPECT(f.outcome == dfa_fidelity_outcome::divergent);
    EXPECT(separates(re, f.witness)); // the witness proves it, on the engine itself
  }
  EXPECT_EQ(real::dfa_faithful(real::regex("a|ab")).witness, std::string("ab"));
  EXPECT_EQ(real::dfa_faithful(real::regex("(?:ab|a)(?:bcdefghijklmnop)?")).witness.size(), std::size_t {16});
}

// And the converse: patterns whose `match()` is always the longest, including a LAZY one — the shape a
// syntactic rule would have refused.
TEST(dfa_faithful_accepts_what_agrees_on_every_input)
{
  const std::vector<std::string> faithful {"x*?y", "a*a", "[a-z]+", "[0-9]+(\\.[0-9]+)?", "'([^']|'')*'",
                                           "(?i)select", "int|in", "(?:a|b)*c", "\\d+"};
  for (const std::string& p : faithful) {
    const real::dfa_fidelity f {real::dfa_faithful(real::regex(p))};
    EXPECT(f.outcome == dfa_fidelity_outcome::faithful);
    EXPECT(f.witness.empty());
  }
}

// The decision replays Pike's closure walk, and one detail of that walk decides answers: a jump back to a
// loop head already visited EXITS the loop at its own priority. Here the body prefers the empty
// iteration, so the engine answers the empty match on "c" while the DFA takes one byte. A decision that
// let the empty iteration die instead would call this faithful.
TEST(dfa_faithful_follows_the_engine_through_an_empty_iteration)
{
  for (const std::string p : {R"((?:c??)*)", R"((?:(?:b){0,2}?)*)", R"((?:(?:(?:a)??)?)*)"}) {
    const real::regex        re {p};
    const real::dfa_fidelity f  {real::dfa_faithful(re)};
    EXPECT(f.outcome == dfa_fidelity_outcome::divergent);
    EXPECT(separates(re, f.witness));
  }
  EXPECT_EQ(real::dfa_faithful(real::regex(R"((?:c??)*)")).witness, std::string("c"));
}

// `\A`/`^` is admitted anywhere, not only at the head: true before the first byte, false after one. The
// decision crosses it on the first closure only, so a pattern that needs it later can never accept there.
TEST(dfa_faithful_crosses_a_start_anchor_only_before_the_first_byte)
{
  const real::regex head {"^(?:a|ab)"};
  EXPECT(real::dfa_faithful(head).outcome == dfa_fidelity_outcome::divergent);
  EXPECT_EQ(real::dfa_faithful(head).witness, std::string("ab"));
  EXPECT(real::dfa_faithful(real::regex(R"(\A[a-z]+)")).outcome == dfa_fidelity_outcome::faithful);

  // Only `a` can follow the anchor's failure: the `^b` branch is dead after a byte, so `a^b|a` is `a`.
  const real::regex late {"a^b|a"};
  EXPECT(real::dfa_faithful(late).outcome == dfa_fidelity_outcome::faithful);
  EXPECT(!separates(late, "ab"));
  const real::regex late_first {"(?:a|a^b)c?"};
  EXPECT(real::dfa_faithful(late_first).outcome == dfa_fidelity_outcome::faithful);
  EXPECT(!separates(late_first, "abc"));
}

// A budget too small to finish is an answer of its own, and it is never `faithful`.
TEST(dfa_faithful_reports_an_exhausted_budget_as_undecided)
{
  const real::regex        re    {"[a-z]+[0-9]+[a-z]+"};
  const real::dfa_fidelity small {real::dfa_faithful(re, 1)};
  EXPECT(small.outcome == dfa_fidelity_outcome::undecided);
  EXPECT(small.witness.empty());
  EXPECT(real::dfa_faithful(re).outcome == dfa_fidelity_outcome::faithful); // the default budget finishes
}

// Over a set: the first pattern not proven faithful, by index. All-faithful is sufficient for the DFA to
// reproduce the per-rule munch, and not necessary — `[a-z]+` outlasts `a|ab` on every input, so this set's
// DFA munch is right, and the set is still refused. That refusal is the documented cost.
TEST(dfa_faithful_over_a_set_names_the_first_rule_not_proven)
{
  const std::vector<real::regex> keywords {real::regex("as"), real::regex("assert"), real::regex("[a-z]+")};
  EXPECT(real::dfa_faithful(keywords).outcome == dfa_fidelity_outcome::faithful);

  const std::vector<real::regex> joined {real::regex("[ ]+"), real::regex("as|assert"), real::regex("[a-z]+")};
  const real::dfa_fidelity       f      {real::dfa_faithful(joined)};
  EXPECT(f.outcome == dfa_fidelity_outcome::divergent);
  EXPECT_EQ(f.rule_index, std::size_t {1});
  EXPECT_EQ(f.witness, std::string("assert"));

  const std::vector<real::regex> masked {real::regex("[a-z]+"), real::regex("a|ab")};
  EXPECT(real::dfa_faithful(masked).outcome == dfa_fidelity_outcome::divergent);
  EXPECT_EQ(real::dfa_faithful(masked).rule_index, std::size_t {1});

  EXPECT(real::dfa_faithful(std::span<const real::regex> {}).outcome == dfa_fidelity_outcome::faithful);
}

// A pattern that is not DFA-able has no fidelity to decide: the same error the constructor raises.
TEST(dfa_faithful_raises_what_the_dfa_would_raise)
{
  EXPECT_THROWS((void)real::dfa_faithful(real::regex("a$")), real::dfa_error);
  EXPECT_THROWS((void)real::dfa_faithful(real::regex("(?=a)a")), real::dfa_error);
}

// The spine: on random patterns, each answer is checked against the engine over EVERY input up to four
// symbols — `faithful` must find no separating input, `divergent` must hand one over. Both outcomes are
// counted, so a generator that stopped producing one of them could not leave this vacuously green.
TEST(dfa_fidelity_agrees_with_the_engine)
{
  const std::vector<std::string> alphabet {"a", "b", "c", "A", "é"};
  std::vector<std::string>       inputs   {""};
  for (std::size_t len = 0, lo = 0; len < 4; ++len) {
    const std::size_t hi {inputs.size()};
    for (std::size_t i = lo; i < hi; ++i) {
      for (const std::string& sym : alphabet) {
        inputs.push_back(inputs[i] + sym);
      }
    }
    lo = hi;
  }

  // Fixed seed: the patterns are part of the test, so a failure names the same pattern every run.
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed)
  std::mt19937 rng       {0xFA17FU};
  std::size_t  faithful  {0};
  std::size_t  divergent {0};
  for (int n = 0; n < 600; ++n) {
    const real::regex        re {random_pattern(rng, 4)};
    const real::dfa_fidelity f  {real::dfa_faithful(re)};
    if (f.outcome == dfa_fidelity_outcome::faithful) {
      ++faithful;
      for (const std::string& text : inputs) {
        EXPECT(!separates(re, text));
      }
    }
    else {
      EXPECT(f.outcome == dfa_fidelity_outcome::divergent);
      ++divergent;
      EXPECT(separates(re, f.witness));
    }
  }
  EXPECT(faithful > 100U);
  EXPECT(divergent > 100U);
}
