//! The Aho-Corasick automaton's memory is bounded by one budget: the dense table where it fits, the sparse trie
//! with dense rows for its shallowest nodes past that, and no automaton past what the trie itself may take. These
//! pin that every form answers as the others and as the ordinary alternation route, which form a budget buys, and
//! what an assignment leaves behind.
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  using span_list = std::vector<std::pair<std::size_t, std::size_t>>;

  span_list spans_of(const real::regex& re,
                     const std::string& s)
  {
    span_list out;
    for (const auto& m : re.find_iter(s)) {
      out.emplace_back(m.start(), m.end());
    }
    return out;
  }

  //! \brief The automaton \p re built on its last search, or null.
  const real::detail::ac_automaton* automaton_of(const real::regex& re)
  {
    const real::detail::regex_immutables* const immut {re.raw_program().immut};
    return immut != nullptr && immut->ac.has_value() ? &*immut->ac : nullptr;
  }

  //! \brief Restores every seam this file moves, whatever a test left them at.
  struct seams_reset
  {
    seams_reset()
    {
      real::detail::ac_density_gate_disabled() = true; // the automaton is taken wherever the route allows it
    }

    seams_reset(const seams_reset&)            = delete;
    seams_reset& operator=(const seams_reset&) = delete;

    ~seams_reset()
    {
      real::detail::ac_density_gate_disabled()     = false;
      real::detail::aho_corasick_route_disabled()  = false;
      real::detail::ac_dense_disabled()            = false;
      real::detail::ac_memory_budget()             = real::detail::ac_memory_budget_default;
      real::detail::ac_sparse_row_cap()            = std::numeric_limits<std::size_t>::max();
    }
  };

  //! \brief \p count distinct lowercase words of \p width letters, from a fixed seed.
  std::vector<std::string> words(std::size_t count,
                                 std::size_t width)
  {
    std::uint32_t            state {0x9E3779B9U};
    std::vector<std::string> out;
    while (out.size() < count) {
      std::string w;
      while (w.size() < width) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        w     += static_cast<char>('a' + (state % 26U));
      }
      out.push_back(std::move(w));
    }
    return out;
  }

  std::string alternation(const std::vector<std::string>& branches)
  {
    std::string out;
    for (const auto& b : branches) {
      out += (out.empty() ? "" : "|") + b;
    }
    return out;
  }

  //! \brief A subject planting \p branches' words, their prefixes and their upper-cased spellings among filler.
  std::string planted(const std::vector<std::string>& branches)
  {
    std::string s;
    for (std::size_t i {0}; i < branches.size() && s.size() < 20000U; i += 7U) {
      const std::string& w {branches[i]};
      s += "..." + w + " " + w.substr(0, w.size() / 2U) + "_";
      std::string upper    {w};
      for (char& c : upper) {
        c = static_cast<char>(c - 'a' + 'A');
      }
      s += upper;
      s += "x";
      s += w;
      s += w.substr(1);
    }
    return s + branches.back();
  }

  //! \brief One way to lay the automaton out, and the form it must then take.
  struct layout
  {
    const char* name;       //!< For the failure message.
    bool        dense_off;  //!< \ref real::detail::ac_dense_disabled.
    std::size_t row_cap;    //!< \ref real::detail::ac_sparse_row_cap.
    std::size_t budget;     //!< \ref real::detail::ac_memory_budget.
    bool        route_off;  //!< \ref real::detail::aho_corasick_route_disabled: the ordinary alternation route.
  };

  constexpr std::size_t no_cap {std::numeric_limits<std::size_t>::max()};

  const std::vector<layout>& layouts()
  {
    static const std::vector<layout> all {
      {.name = "dense", .dense_off = false, .row_cap = no_cap, .budget = real::detail::ac_memory_budget_default, .route_off = false},
      {.name = "sparse, rows to the budget", .dense_off = true, .row_cap = no_cap, .budget = real::detail::ac_memory_budget_default, .route_off = false},
      {.name = "sparse, root row only", .dense_off = true, .row_cap = 0, .budget = real::detail::ac_memory_budget_default, .route_off = false},
      {.name = "sparse, one row past the root", .dense_off = true, .row_cap = 1, .budget = real::detail::ac_memory_budget_default, .route_off = false},
      {.name = "declined", .dense_off = false, .row_cap = no_cap, .budget = 64, .route_off = false},
      {.name = "alternation route", .dense_off = false, .row_cap = no_cap, .budget = real::detail::ac_memory_budget_default, .route_off = true}};
    return all;
  }

  //! Case folding the fixed-alternation shape keeps: ASCII only, each letter one class of two bytes.
  constexpr real::flags folded_ascii {real::flags::icase | real::flags::ascii};

  //! \brief \p pattern's spans on \p s under \p l, from a regex built under it, with the form it took checked.
  span_list spans_under(const layout&      l,
                        const std::string& pattern,
                        real::flags        f,
                        const std::string& s)
  {
    real::detail::ac_dense_disabled()           = l.dense_off;
    real::detail::ac_sparse_row_cap()           = l.row_cap;
    real::detail::ac_memory_budget()            = l.budget;
    real::detail::aho_corasick_route_disabled() = l.route_off;
    const real::regex re  {pattern, f};
    const span_list   out {spans_of(re, s)};
    const auto* const ac  {automaton_of(re)};
    if (l.route_off || l.budget < 1024U) {
      EXPECT(ac == nullptr);
    }
    else if (ac == nullptr) {
      std::printf("/%.60s/ under %s: no automaton\n", pattern.c_str(), l.name);
      EXPECT(ac != nullptr);
    }
    else {
      EXPECT_EQ(ac->is_sparse(), l.dense_off);
      if (l.dense_off && l.row_cap != no_cap) {
        EXPECT(ac->sparse_rows() <= l.row_cap + 1U);
        EXPECT(ac->sparse_rows() >= 1U);
      }
    }
    return out;
  }
} // namespace

// Every layout answers as every other and as the ordinary alternation route: branches that prefix one another
// (`abcd|ab` on `abcd`, the first listed winning at one start), a branch found only down another's output link
// (`bc` in `abcy` against `abcx`), a longer first-listed branch over its own suffix, word boundaries, case folding, and random words planted with their prefixes.
TEST(ac_every_layout_answers_alike)
{
  const seams_reset                                              guard;
  std::vector<std::string>                                       padding {words(20, 5)};
  std::vector<std::tuple<std::string, real::flags, std::string>> cases;
  {
    std::vector<std::string> b {"abcd", "ab"};
    b.insert(b.end(), padding.begin(), padding.end());
    cases.emplace_back(alternation(b), real::flags::none, "abcd ab xabcdx abcab " + planted(padding));
    std::vector<std::string> r {"ab", "abcd"};
    r.insert(r.end(), padding.begin(), padding.end());
    cases.emplace_back(alternation(r), real::flags::none, "abcd ab xabcdx abcab " + planted(padding));
    std::vector<std::string> o {"abcx", "bc"}; // `bc` inside `abcy` is reached only through `abc`'s output link
    o.insert(o.end(), padding.begin(), padding.end());
    cases.emplace_back(alternation(o), real::flags::none, "abcy abcx bc " + planted(padding));
  }
  cases.emplace_back("category|cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger|zebra|camel|otter|mouse",
                     real::flags::none, "category cat categor dogfish tigers");
  cases.emplace_back("cat|category|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger|zebra|camel|otter|mouse",
                     real::flags::none, "category cat categor dogfish tigers");
  cases.emplace_back("\\b(?:" + alternation(words(40, 4)) + ")\\b", real::flags::none, planted(words(40, 4)));
  cases.emplace_back(alternation(words(60, 6)), folded_ascii, planted(words(60, 6)));
  cases.emplace_back(alternation(words(500, 3)), real::flags::none, planted(words(500, 3)));
  cases.emplace_back(alternation(words(300, 12)), real::flags::none, planted(words(300, 12)));
  std::size_t checked {0};
  for (const auto& [pattern, f, subject] : cases) {
    EXPECT(real::detail::dynamic_storage::compile(pattern, f).program.hints.fixed_alternation);
    const span_list want {spans_under(layouts().back(), pattern, f, subject)};
    EXPECT(!want.empty());
    for (const layout& l : layouts()) {
      const span_list got {spans_under(l, pattern, f, subject)};
      if (got != want) {
        std::printf("/%.60s/ under %s: %zu spans, the alternation route %zu\n", pattern.c_str(), l.name, got.size(),
                    want.size());
      }
      EXPECT(got == want);
      ++checked;
    }
  }
  EXPECT_EQ(checked, cases.size() * layouts().size());
}

// The budget buys the form: the dense table where it fits, the sparse trie with fewer rows than nodes where only
// the trie does, the root's row alone under a cap of none, and no automaton below the trie.
TEST(ac_budget_buys_the_form)
{
  const seams_reset              guard;
  const std::vector<std::string> branches {words(2000, 10)};
  const std::string              pattern  {alternation(branches)};
  const std::string              subject  {planted(branches)};

  const real::regex dense                 {pattern};
  const span_list   want                  {spans_of(dense, subject)};
  const auto* const full                  {automaton_of(dense)};
  EXPECT(full != nullptr);
  if (full == nullptr) {
    return;
  }
  const std::size_t nodes {full->node_count()};
  EXPECT(!full->is_sparse());
  EXPECT(full->memory_bytes() < nodes * 160U); // 26 classes and the reporting arrays, not a 256-wide row per node
  EXPECT(full->memory_bytes() >= nodes * 26U * sizeof(std::int32_t));

  real::detail::ac_memory_budget() = full->memory_bytes() - 1U;
  const real::regex shrunk {pattern};
  EXPECT(spans_of(shrunk, subject) == want);
  const auto* const sparse {automaton_of(shrunk)};
  EXPECT(sparse != nullptr && sparse->is_sparse());
  EXPECT(sparse != nullptr && sparse->sparse_rows() > 1U && sparse->sparse_rows() < nodes);
  EXPECT(sparse != nullptr && sparse->memory_bytes() <= real::detail::ac_memory_budget());

  real::detail::ac_sparse_row_cap() = 0;
  const real::regex rootonly {pattern};
  EXPECT(spans_of(rootonly, subject) == want);
  EXPECT(automaton_of(rootonly) != nullptr && automaton_of(rootonly)->sparse_rows() == 1U);
  real::detail::ac_sparse_row_cap() = no_cap;

  real::detail::ac_memory_budget() = nodes * 4U; // below the trie itself
  const real::regex declined {pattern};
  EXPECT(spans_of(declined, subject) == want);
  EXPECT(automaton_of(declined) == nullptr);
}

// A case-folded alternation is one path per branch over classes, not one per spelling: sixteen letters would
// be 65 536 spellings a branch, past any expansion, and the trie is the case-sensitive one's.
TEST(ac_case_folded_branches_share_one_path)
{
  const seams_reset              guard;
  const std::vector<std::string> branches {words(400, 16)};
  const std::string              subject  {planted(branches)};
  const real::regex              folded   {alternation(branches), folded_ascii};
  const real::regex              exact    {alternation(branches)};
  EXPECT(folded.count_matches(subject) > exact.count_matches(subject));
  const auto* const f                     {automaton_of(folded)};
  const auto* const e                     {automaton_of(exact)};
  EXPECT(f != nullptr && e != nullptr);
  if (f != nullptr && e != nullptr) {
    EXPECT_EQ(f->node_count(), e->node_count());
    EXPECT(!f->is_sparse());
  }
  real::detail::aho_corasick_route_disabled() = true;
  const real::regex walk {alternation(branches), folded_ascii};
  EXPECT(spans_of(walk, subject) == spans_of(folded, subject));
}

// Assigning a small pattern over a large alternation releases the automaton, not only its key.
TEST(ac_assignment_releases_the_automaton)
{
  const seams_reset              guard;
  const std::vector<std::string> branches {words(2000, 10)};
  real::regex                    re       {alternation(branches)};
  EXPECT(re.count_matches(planted(branches)) > 0U);
  EXPECT(automaton_of(re) != nullptr);
  re = real::regex {"x"};
  EXPECT(automaton_of(re) == nullptr);
  EXPECT_EQ(re.count_matches("axbx"), 2U);
}

// An alternation of more branches than its count holds reads as that many, not as the count wrapped to a few.
TEST(ac_branch_count_saturates)
{
  std::string pattern {"[ab]"};
  for (int i {1}; i < 65537; ++i) {
    pattern += "|[ab]";
  }
  const auto hints {real::detail::dynamic_storage::compile(pattern, real::flags::none).program.hints};
  EXPECT(hints.fixed_alternation);
  EXPECT_EQ(hints.alternation_branch_count, std::numeric_limits<std::uint16_t>::max());
  const real::regex re {pattern};
  EXPECT_EQ(re.count_matches("ab_ba_c"), 4U);
}
