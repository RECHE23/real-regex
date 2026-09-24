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
  for (const auto& rules : rule_sets()) {
    const real::dfa machine {build(rules)};
    for (int round = 0; round < 60; ++round) {
      std::string subject(static_cast<std::size_t>(rng() % 40), 'a');
      for (char& c : subject) {
        c = "abcxy"[rng() % 5];
      }
      // A lexing sequence: advance by the answer, or by one byte where nothing matched.
      real::dfa_munch_memo lexing {subject.size()};
      for (std::size_t at = 0; at < subject.size();) {
        const auto memoized {machine.match(subject, at, lexing)};
        EXPECT(same(memoized, machine.match(std::string_view(subject).substr(at))));
        ++compared;
        at += memoized ? memoized->length : 1;
      }
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
