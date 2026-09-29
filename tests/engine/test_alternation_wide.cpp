//! An alternation of literals with more first bytes than the small set holds: the nibble fingerprint scans it
//! where its sample finds false candidates sparse, and the automaton's gate decides the rest as before. These pin
//! its answers against the routes it replaces, where it is taken and where it declines, and the fields it reads.
#include <cstdint>
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
                     const std::string& s)
  {
    span_list out;
    for (const auto& m : re.find_iter(s)) {
      out.emplace_back(m.start(), m.end());
    }
    return out;
  }

  //! \brief Whether the fingerprint runs here (AArch64, x86 with SSSE3 in the build or found at run time).
  bool fingerprint_here()
  {
    return real::detail::alternation_nibbles_supported();
  }

  //! \brief A subject of at least \p size bytes repeating \p unit.
  std::string repeated(std::string_view unit,
                       std::size_t      size)
  {
    std::string s;
    while (s.size() < size) {
      s += unit;
    }
    return s;
  }

  //! \brief Scans taken by the wide route over one count on \p s, with the automaton's verdict armed first.
  std::uint64_t wide_scans_for(const real::regex& re,
                               const std::string& s)
  {
    real::detail::alternation_wide_scans() = 0;
    real::detail::ac_density_last_verdict().store(real::detail::ac_verdict::not_consulted);
    static_cast<void>(re.count_matches(s));
    return real::detail::alternation_wide_scans().load();
  }

  real::detail::pattern_hints hints_of(std::string_view pattern)
  {
    return real::detail::dynamic_storage::compile(pattern, real::flags::none).program.hints;
  }

  constexpr std::string_view twelve_none  {"cqz|dqz|fqz|bqz|tqz|wqz|sqz|hqz|oqz|aqz|eqz|iqz"};
  constexpr std::string_view twelve_words {"the|and|for|with|not|but|some|just|more|here|words|all"};
} // namespace

// Random alternations of 9 to 16 branches with varied first bytes -- prefixes of one another, a one-byte branch
// now and then, some inside word boundaries -- over subjects past and short of the sample's floor, with matches
// planted in the last blocks: the wide route answers as the automaton, the first-byte walk and the 16-start scan.
TEST(alternation_wide_answers_as_the_routes_it_replaces)
{
  std::uint32_t state {0x2545F491U};
  const auto    next  {[&state] {
                         state ^= state << 13U;
                         state ^= state >> 17U;
                         state ^= state << 5U;
                         return state;
                       }};
  const std::string letters {"abcdefghijklmnop"};
  std::size_t       taken   {0};
  std::size_t       cases   {0};
  for (int round {0}; round < 60; ++round) {
    const std::size_t branches {9U + (next() % 8U)};
    std::string       pattern;
    for (std::size_t b {0}; b < branches; ++b) {
      std::string       branch {letters[b]}; // distinct first bytes: more than the small set holds
      const std::size_t width  {(next() % 40U == 0U) ? 1U : 2U + (next() % 4U)};
      while (branch.size() < width) {
        branch += letters[next() % 6U];
      }
      if (b > 0 && next() % 5U == 0U) {
        branch = pattern.substr(pattern.rfind('|') + 1U) + branch.substr(0, 1); // a branch its predecessor prefixes
      }
      pattern += (b == 0 ? "" : "|") + branch;
    }
    if (next() % 4U == 0U) {
      pattern.insert(0, "\\b(?:");
      pattern.append(")\\b");
    }
    const real::regex re {pattern};
    for (const std::size_t size : {std::size_t {3000}, std::size_t {9000}}) {
      std::string s;
      while (s.size() < size) {
        // Mostly bytes no branch opens on, so the sample finds false candidates sparse; a branch letter now and then.
        const std::uint32_t r {next() % 16U};
        if (r == 0U) {
          s += static_cast<char>('a' + (next() % 16U));
        }
        else {
          s += r < 4U ? ' ' : static_cast<char>('q' + (next() % 10U));
        }
      }
      s += " " + pattern.substr(pattern.rfind('|') + 1U, 3) + " zz"; // a match in the last bytes
      const std::uint64_t scans {wide_scans_for(re, s)};
      const span_list     wide  {spans_of(re, s)};
      real::detail::alternation_pairs_disabled() = true;             // the wide route declines: the automaton's gate decides
      const span_list gate      {spans_of(re, s)};
      real::detail::aho_corasick_route_disabled() = true;
      const span_list walk      {spans_of(re, s)};
      real::detail::alternation_pairs_disabled()  = false;
      real::detail::aho_corasick_route_disabled() = false;
      real::detail::alternation_avx2_disabled()   = true;
      const span_list narrow {spans_of(re, s)};
      real::detail::alternation_avx2_disabled() = false;
      if (wide != gate || wide != walk || wide != narrow) {
        std::printf("/%s/ on %zu bytes: wide %zu, gate %zu, walk %zu, narrow %zu matches\n", pattern.c_str(), s.size(),
                    wide.size(), gate.size(), walk.size(), narrow.size());
      }
      EXPECT(wide == gate);
      EXPECT(wide == walk);
      EXPECT(wide == narrow);
      EXPECT_EQ(re.count_matches(s), gate.size());
      if (!re.search(s, 1).matched()) {
        EXPECT(gate.empty() || gate.front().first == 0U);
      }
      taken += scans > 0U ? 1U : 0U;
      ++cases;
    }
  }
  std::printf("  wide route: %zu of %zu cases took it\n", taken, cases);
  EXPECT_EQ(cases, 120U);
  EXPECT(fingerprint_here() ? taken >= 20U : taken == 0U);
}

// The route takes a subject with no match past the sample's floor, and the automaton's gate is never asked.
TEST(alternation_wide_takes_a_sparse_subject_from_the_automaton)
{
  const real::regex re {twelve_none};
  const std::string s  {repeated("some ordinary prose without anything interesting in it at all, just words. ", 20000)};
  EXPECT_EQ(hints_of(twelve_none).small_set_size, 0U);
  real::detail::alternation_nibble_blocks() = 0;
  EXPECT_EQ(wide_scans_for(re, s), fingerprint_here() ? 1U : 0U);
  EXPECT_EQ(re.count_matches(s), 0U);
  if (fingerprint_here()) {
    EXPECT(real::detail::alternation_nibble_blocks().load() > 500U);
    EXPECT(real::detail::ac_density_last_verdict().load() == real::detail::ac_verdict::not_consulted);
  }
}

// Where it declines, and the automaton's gate is asked as before: seventeen branches (past the plan), ten branches
// on one first byte (its own scan by that byte), case folding (classes, no plan), a one-byte branch (no fingerprint), a subject short of the floor, and
// false candidates too dense for the sample.
TEST(alternation_wide_declines_what_it_cannot_scan_cheaper)
{
  const std::string                                      prose    {repeated("some ordinary prose without anything interesting in it at all, just words. ", 20000)};
  const std::vector<std::pair<std::string, std::string>> declined {
    {"cqz|dqz|fqz|bqz|tqz|wqz|sqz|hqz|oqz|aqz|eqz|iqz|gqz|jqz|kqz|lqz|mqz", prose},
    {"cqa|cqb|cqc|cqd|cqe|cqf|cqg|cqh|cqi|cqj", prose},
    {"(?i)cqz|dqz|fqz|bqz|tqz|wqz|sqz|hqz|oqz|aqz|eqz|iqz", prose},
    {"c|dqz|fqz|bqz|tqz|wqz|sqz|hqz|oqz|aqz|eqz|iqz", prose},
    {std::string {twelve_none}, prose.substr(0, 3000)},
    {std::string {twelve_words}, repeated("witx", 20000)}};
  for (const auto& [pattern, subject] : declined) {
    const real::regex re {pattern};
    EXPECT_EQ(wide_scans_for(re, subject), 0U);
    real::detail::alternation_pairs_disabled() = true;
    const std::size_t want {re.count_matches(subject)};
    real::detail::alternation_pairs_disabled() = false;
    EXPECT_EQ(re.count_matches(subject), want);
  }
}

// The sample's budget, from both sides: twelve short branches with a false candidate every 16 bytes cost more
// verifying than the automaton's walk; every 32 bytes, less.
TEST(alternation_wide_budget_splits_false_candidate_densities)
{
  const real::regex re {twelve_words};
  EXPECT_EQ(wide_scans_for(re, repeated("witx____________", 20000)), 0U);
  EXPECT_EQ(wide_scans_for(re, repeated("witx____________________________", 20000)), fingerprint_here() ? 1U : 0U);
  EXPECT_EQ(re.count_matches(repeated("witx____________________________ with", 20000)), 541U);
}

// The fields the route reads keep their meaning for their other readers: more first bytes than the small set
// holds leave `small_set_size` at 0, and a set whose member is such an alternation answers as that member alone.
TEST(alternation_wide_leaves_the_small_set_fields_and_sets_alone)
{
  const real::regex nine {"ab|cd|ef|gh|ij|kl|mn|op|qr"};
  EXPECT_EQ(hints_of("ab|cd|ef|gh|ij|kl|mn|op|qr").small_set_size, 0U);
  EXPECT(hints_of("ab|cd|ef|gh|ij|kl|mn|op|qr").single_first < 0);
  const std::string        s     {repeated("the quick brown fox jumps over the lazy dog; op qr ", 20000)};
  const real::regex        words {twelve_words};
  const real::regex_set    set   {"the|and|for|with|not|but|some|just|more|here|words|all", "zzzz"};
  std::size_t              hits  {0};
  for (std::size_t at {0}; at < 200U; at += 50U) {
    hits += set.matches(std::string_view {s}.substr(at)).empty() ? 0U : 1U;
  }
  EXPECT_EQ(hits, 4U);
  EXPECT(words.count_matches(s) > 500U);
  EXPECT_EQ(nine.count_matches(s), spans_of(nine, s).size());
}

// The compile-time storage has no plan: its answers do not move.
TEST(alternation_wide_static_regex_answers_the_same)
{
  static constexpr real::static_regex<"the|and|for|with|not|but|some|just|more|here|words|all"> fixed;
  const real::regex                                                                             dynamic {twelve_words};
  const std::string                                                                             s       {repeated("some ordinary prose with words and more words here for all. ", 10000)};
  EXPECT_EQ(fixed.count_matches(s), dynamic.count_matches(s));
}

// Assigning one wide alternation over another on a warmed regex: the plan follows the program, not the object.
TEST(alternation_wide_plan_follows_an_assignment)
{
  real::regex       re {twelve_none};
  const std::string s  {repeated("some ordinary prose with words and more words here for all. ", 10000)};
  EXPECT_EQ(re.count_matches(s), 0U);
  re                                         = real::regex {twelve_words};
  real::detail::alternation_pairs_disabled() = true;
  const std::size_t want {re.count_matches(s)};
  real::detail::alternation_pairs_disabled() = false;
  EXPECT(want > 1000U);
  EXPECT_EQ(re.count_matches(s), want);
}
