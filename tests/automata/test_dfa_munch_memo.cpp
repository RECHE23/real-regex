// real::dfa_munch_memo -- successive munches over one subject in linear total time (Reps 1998).
//
// The memo may only shorten walks, never change an answer: every memoized munch is compared with the
// plain one on the same suffix, over lexing sequences and over offsets in random order. The bound is
// measured in DFA transitions, an exact count, on the input that makes the plain munch quadratic.
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/dfa.hpp"

namespace {

  bool same(const std::optional<real::dfa_match>& a,
            const std::optional<real::dfa_match>& b)
  {
    return a.has_value() == b.has_value()
           && (!a.has_value() || (a->rule_index == b->rule_index && a->length == b->length));
  }

  real::dfa build(const std::vector<std::string>& sources)
  {
    std::vector<real::regex> pats;
    pats.reserve(sources.size());
    for (const std::string& src : sources) {
      pats.emplace_back(src);
    }
    return real::dfa(pats);
  }

  std::vector<std::vector<std::string>> rule_sets()
  {
    return {
      {"a*b", "a"},
      {"[ab]*c", "[ab]", "b+"},
      {"(ab)*abc", "ab", "a"},
      {"a+", "a*ba", "b"},
      {"x[a-c]*y", "[a-c]+", "x"},
    };
  }
} // namespace

TEST(memoized_munch_answers_what_the_plain_munch_answers)
{
  // Fixed seed: the subjects are part of the test, so a failure names the same one every run.
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed)
  std::mt19937 rng      {0x4E75U};
  std::size_t  compared {0};
  std::size_t  armed    {0};
  for (const auto& rules : rule_sets()) {
    const real::dfa machine {build(rules)};
    for (int round = 0; round < 90; ++round) {
      // Short subjects over the whole alphabet, then long ones that are mostly `a`: those make the long
      // dead stretches that arm the memo, so both the unarmed and the armed walk are compared.
      const bool  long_subject {round >= 60};
      std::string subject(long_subject ? 100 + (rng() % 300) : rng() % 40, 'a');
      for (char& c : subject) {
        if (!long_subject) {
          c = "abcxy"[rng() % 5];
        }
        else if (rng() % 50 == 0) {
          c = "bcxy"[rng() % 4];
        }
      }
      // A lexing sequence: advance by the answer, or by one byte where nothing matched.
      real::dfa_munch_memo lexing {subject.size()};
      for (std::size_t at = 0; at < subject.size();) {
        const auto memoized {machine.match(subject, at, lexing)};
        EXPECT(same(memoized, machine.match(std::string_view(subject).substr(at))));
        ++compared;
        at += memoized ? memoized->length : 1;
      }
      armed += static_cast<std::size_t>(lexing.armed());
      // Offsets in random order: what the memo records holds for any later munch, not only the next.
      real::dfa_munch_memo shuffled {subject.size()};
      for (int k = 0; k < 20; ++k) {
        const std::size_t at {subject.empty() ? 0 : rng() % (subject.size() + 1)};
        EXPECT(same(machine.match(subject, at, shuffled), machine.match(std::string_view(subject).substr(at))));
        ++compared;
      }
    }
  }
  EXPECT(compared > 5000);
  EXPECT(armed > 20); // the armed walk is compared too, not only the plain one
}

TEST(memoized_lexing_is_linear_where_the_plain_one_is_quadratic)
{
  // `a*b` beside `a` over "aaa…": each plain munch walks to the end looking for the b, so lexing n
  // bytes costs n(n+1)/2 transitions. With the memo the first walk reads n bytes and its replay marks
  // the n - 1 pairs after its accept; every later munch then takes three: the `a` it accepts, the pair
  // the first walk proved dead, and the one-step replay that re-marks it.
  const real::dfa          machine {build({"a*b", "a"})};
  std::vector<std::size_t> work;
  for (const std::size_t n : {1000U, 2000U, 4000U}) {
    const std::string    subject(n, 'a');
    real::dfa_munch_memo memo   {n};
    std::size_t          tokens {0};
    for (std::size_t at = 0; at < n; ++tokens) {
      const auto m {machine.match(subject, at, memo)};
      EXPECT(m.has_value() && m->rule_index == 1U && m->length == 1U);
      at += m ? m->length : 1;
    }
    EXPECT(tokens == n);
    EXPECT(memo.armed()); // the first walk's dead stretch runs the whole subject
    work.push_back(memo.transitions());
  }
  EXPECT(work[0] <= std::size_t {5000}); // against 500 500 for the plain munch
  EXPECT(work[1] <= std::size_t {10000});
  EXPECT(work[2] <= std::size_t {20000});
  EXPECT(work[2] >= 4000);               // the first walk alone reads every byte: the count is not vacuous
}

TEST(a_memo_is_bound_to_its_subject_and_its_dfa)
{
  const real::dfa      first  {build({"a+"})};
  const real::dfa      second {build({"b+"})};
  real::dfa_munch_memo memo   {3};
  const auto           refused {[](auto&& call) {
                                  try {
                                    static_cast<void>(call());
                                  }
                                  catch (const std::invalid_argument&) {
                                    return true;
                                  }
                                  return false;
                                }};
  EXPECT(refused([&] { return first.match("aaaa", 0, memo); }));  // another length
  EXPECT(refused([&] { return first.match("aaa", 4, memo); }));   // past the end
  EXPECT(first.match("aaa", 3, memo) == std::nullopt);            // at the end: nothing to match
  EXPECT(refused([&] { return second.match("aaa", 0, memo); }));  // another DFA
}

TEST(the_stretch_is_marked_from_the_state_the_walk_really_stood_on)
{
  // `c` accepts after one byte and `ca*b` carries the walk on through the a's to a dead end at the d:
  // a long dead stretch, marked from the state the walk stood on after `c`. Marked from the start
  // state instead, the stretch would claim that `a*d`'s states lead nowhere -- and the next munch,
  // which starts right there, would lose the a…ad token that `a*d` matches.
  const real::dfa         machine {build({"c", "ca*b", "a*d"})};
  const std::string       subject {"c" + std::string(50, 'a') + "d"};
  real::dfa_munch_memo    memo    {subject.size()};
  const auto              first   {machine.match(subject, 0, memo)};
  EXPECT(first.has_value() && first->rule_index == 0U && first->length == 1U);
  EXPECT(memo.armed());
  const auto              second {machine.match(subject, 1, memo)};
  EXPECT(second.has_value() && second->rule_index == 2U && second->length == 51U);
}

// dfa::munch: the same answer as match, and a promise -- when it says no text after the subject can change the
// answer, none does. Checked at every offset of random subjects against continuations of a few bytes, with the
// memo armed by the earlier munches as a lexer arms it (long subjects arm it; short ones do not).
TEST(dfa_munch_answers_as_match_and_keeps_its_promise)
{
  const std::string_view alphabet      {"abc \n"};
  std::mt19937           rng           {20261010U};
  std::size_t            final_answers {0};
  std::size_t            open_answers  {0};
  std::size_t            armed         {0};
  // `a[ab]*c` walks far over `abab...` without an accept: to a death at a space (a mark that holds whatever
  // follows) or alive to the end (a mark for this subject only).
  std::vector<std::vector<std::string>> sets {rule_sets()};
  sets.push_back({"a[ab]*c", "[ab]", " "});
  for (const std::vector<std::string>& sources : sets) {
    const real::dfa d {build(sources)};
    for (int round {0}; round < 60; ++round) {
      // Short subjects, and long ones whose dead stretches (past dfa_munch_memo::short_stretch) arm the
      // memo, so that later munches stop on its marks.
      std::string       text;
      const std::size_t length {round % 2 == 0 ? rng() % 12 : 40 + (rng() % 60)};
      const bool        dense  {round % 4 == 1}; // long runs of a and b, a space now and then
      for (std::size_t n {length}; n > 0; --n) {
        text += dense ? (rng() % 50 == 0 ? ' ' : "ab"[rng() % 2]) : alphabet[rng() % alphabet.size()];
      }
      real::dfa_munch_memo memo  {text.size()};
      real::dfa_munch_memo plain {text.size()};
      for (std::size_t offset {0}; offset <= text.size(); ++offset) {
        const real::dfa_munch said {d.munch(text, offset, memo)};
        EXPECT(same(said.match, d.match(text, offset, plain)));
        armed += memo.armed() ? 1U : 0U;
        if (said.more_text_may_change) {
          ++open_answers;
          continue;
        }
        ++final_answers;
        for (int extension {0}; extension < 20; ++extension) {
          std::string longer {text};
          for (std::size_t n {1 + (rng() % 4)}; n > 0; --n) {
            longer += alphabet[rng() % alphabet.size()];
          }
          EXPECT(same(said.match, d.match(std::string_view {longer}.substr(offset))));
        }
      }
    }
  }
  EXPECT(final_answers > 200U); // the promise is put to the test
  EXPECT(open_answers > 50U);   // and the walk that reaches the end does say so
  EXPECT(armed > 100U);         // with munches that met the memo's marks
}

// A munch the memo stops on a stretch walked to a death is final, as the walk it shortens would have died too:
// every munch before the space of `a(ab)…ab ab` is, once the first one armed the memo. A stretch walked alive to
// the subject's end is dead for this subject only, and a munch stopped on it may still change.
TEST(dfa_munch_is_final_on_a_mark_from_a_walk_that_died)
{
  const real::dfa d   {build({"a[ab]*c", "[ab]", " "})};
  std::string     run {"a"};
  for (int i {0}; i < 40; ++i) {
    run += "ab";
  }
  const std::string    died {run + " ab"};
  real::dfa_munch_memo memo {died.size()};
  for (std::size_t offset {0}; offset < run.size(); ++offset) {
    EXPECT(!d.munch(died, offset, memo).more_text_may_change);
  }
  EXPECT(memo.armed());
  real::dfa_munch_memo open_memo {run.size()};
  for (std::size_t offset {0}; offset < run.size(); ++offset) {
    // From an `a`, `c` may still come; from a `b` no rule goes past it, and the walk dies on the next byte --
    // but on the last `b` there is no next byte: alive at the end, it may change.
    EXPECT_EQ(d.munch(run, offset, open_memo).more_text_may_change, run[offset] == 'a' || offset + 1 == run.size());
  }
  EXPECT(open_memo.armed());
}
