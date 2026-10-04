//! An alternation of literals with more first bytes than the small set holds: the nibble fingerprint scans it
//! where its sample finds false candidates sparse, and the automaton's gate decides the rest as before. These pin
//! its answers against the routes it replaces, where it is taken and where it declines, and the fields it reads.
#include <array>
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
    real::detail::tally(real::detail::counter::alternation_wide_scans) = 0;
    real::detail::ac_density_last_verdict().store(real::detail::ac_verdict::not_consulted);
    static_cast<void>(re.count_matches(s));
    return real::detail::tally(real::detail::counter::alternation_wide_scans).load();
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
  real::detail::tally(real::detail::counter::alternation_nibble_blocks) = 0;
  EXPECT_EQ(wide_scans_for(re, s), fingerprint_here() ? 1U : 0U);
  EXPECT_EQ(re.count_matches(s), 0U);
  if (fingerprint_here()) {
    EXPECT(real::detail::tally(real::detail::counter::alternation_nibble_blocks).load() > 500U);
    EXPECT(real::detail::ac_density_last_verdict().load() == real::detail::ac_verdict::not_consulted);
  }
}

// Where it declines, and the automaton's gate is asked as before: seventeen branches (past the plan), ten branches
// on one first byte (its own scan by that byte), case folding that reaches a non-ASCII code point (`i` folds to
// `ı` and `İ`: not a fixed alternation), a one-byte branch (no fingerprint), a subject short of the floor, and
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

// Branches that open on a class -- case folding, or a class written out -- have no pairs, and the fingerprint
// carries them alone. Random alternations of 2 to 16 such branches, folded or not, some in word boundaries, over
// subjects dense in their first bytes with matches of flipped case planted, the last one near the end: the
// fingerprint answers as the first-byte walk, the pairs-less decision and the automaton.
TEST(alternation_class_heads_answer_as_the_routes_they_replace)
{
  std::uint32_t state {0x6A09E667U};
  const auto    next  {[&state] {
                         state ^= state << 13U;
                         state ^= state >> 17U;
                         state ^= state << 5U;
                         return state;
                       }};
  const std::string letters     {"cdfqzabCDFQZ19-"};
  const std::string filler      {"cdfabCDFAB qz19-."};
  std::size_t       fingerprint {0};
  std::size_t       wide        {0};
  for (int round {0}; round < 300; ++round) {
    const std::size_t        branches {2U + (next() % 15U)};
    std::vector<std::string> body;
    std::string              alternation;
    for (std::size_t b {0}; b < branches; ++b) {
      std::string       branch;
      const std::size_t width {next() % 8U == 0U ? 1U + (next() % 4U) : 2U + (next() % 3U)};
      while (branch.size() < width) {
        branch += letters[next() % letters.size()];
      }
      if (branch[0] == '-') {
        branch[0] = 'z';
      }
      if (b > 0) {
        alternation += '|';
      }
      alternation += branch;
      body.push_back(branch);
    }
    if (next() % 5U == 0U) {
      alternation.insert(0, "\\b(?:");
      alternation.append(")\\b");
    }
    const std::uint32_t form    {next() % 8U};
    std::string         pattern;
    if (form < 5U) {
      pattern = "(?i)";
    }
    else if (form == 5U) {
      pattern = "[a-d]q|"; // a class head written out, case kept
    }
    pattern += alternation;
    std::string       s;
    const std::size_t size {4096U + (next() % 9000U)};
    while (s.size() < size) {
      s += filler[next() % filler.size()];
    }
    const std::size_t planted {next() % 6U};
    for (std::size_t k {0}; k < planted; ++k) {
      std::string x {body[next() % body.size()]};
      for (char& c : x) {
        if (next() % 2U == 0U && c >= 'a' && c <= 'z') {
          c = static_cast<char>(c - 'a' + 'A');
        }
      }
      const std::size_t at {k == 0 ? s.size() - x.size() - (next() % 40U) : next() % (s.size() - x.size())};
      s.replace(at, x.size(), x);
    }
    const real::regex re {pattern};
    real::detail::tally(real::detail::counter::alternation_nibble_blocks) = 0;
    real::detail::tally(real::detail::counter::alternation_wide_scans)    = 0;
    const span_list got {spans_of(re, s)};
    fingerprint                               += real::detail::tally(real::detail::counter::alternation_nibble_blocks).load() != 0U ? 1U : 0U;
    wide                                      += real::detail::tally(real::detail::counter::alternation_wide_scans).load() != 0U ? 1U : 0U;
    real::detail::alternation_pairs_disabled() = true;
    const span_list walk {spans_of(re, s)};
    real::detail::alternation_pairs_disabled()   = false;
    real::detail::alternation_nibbles_disabled() = true;
    const span_list pairs_only {spans_of(re, s)};
    real::detail::alternation_nibbles_disabled() = false;
    real::detail::aho_corasick_route_disabled()  = true;
    const span_list no_automaton {spans_of(re, s)};
    real::detail::aho_corasick_route_disabled() = false;
    if (got != walk || got != pairs_only || got != no_automaton) {
      std::printf("/%s/ on %zu bytes: %zu, walk %zu, pairs %zu, no automaton %zu\n", pattern.c_str(), s.size(),
                  got.size(), walk.size(), pairs_only.size(), no_automaton.size());
    }
    EXPECT(got == walk);
    EXPECT(got == pairs_only);
    EXPECT(got == no_automaton);
  }
  std::printf("  class heads: %zu of 300 cases fingerprinted, %zu by the wide route\n", fingerprint, wide);
  EXPECT(fingerprint_here() ? fingerprint >= 60U && wide >= 20U : fingerprint == 0U && wide == 0U);
}

// Where branches open on classes, the fingerprint takes their dense subjects: three folded branches (a small set
// of six first bytes), five (ten: past the small set, the wide route), a class written out, and two branches --
// on x86 two pairs would beat a fingerprint, but there are no pairs here.
TEST(alternation_class_heads_take_the_fingerprint)
{
  std::string dense                                             {repeated("dab cfd adc fbd tqb BqA ", 20000)};
  dense += " cQz FQZ tqz aqz";
  const std::vector<std::pair<std::string, std::size_t>> shapes {
    {"(?i)cqz|dqz|fqz", 2U}, {"(?i)cqz|dqz|fqz|bqz|tqz", 3U}, {"[a-z]qz|dqz", 2U}, {"(?i)cq|fq", 2U}};
  for (const auto& [pattern, want] : shapes) {
    const real::regex re {pattern};
    real::detail::tally(real::detail::counter::alternation_nibble_blocks) = 0;
    EXPECT_EQ(re.count_matches(dense), want);
    EXPECT(fingerprint_here() ? real::detail::tally(real::detail::counter::alternation_nibble_blocks).load() > 500U
                              : real::detail::tally(real::detail::counter::alternation_nibble_blocks).load() == 0U);
  }
  // One scan per search: each match found starts the next.
  EXPECT_EQ(wide_scans_for(real::regex {"(?i)cqz|dqz|fqz|bqz|tqz"}, dense), fingerprint_here() ? 4U : 0U);
  EXPECT_EQ(wide_scans_for(real::regex {"[a-z]qz|dqz"}, dense), fingerprint_here() ? 3U : 0U);
}

// A plan without pairs never runs them: the decision is kept for the subject, and a fill that meets the
// fingerprint switched off after it falls to the first bytes, not to pairs whose lead bytes are unset.
TEST(alternation_without_pairs_never_runs_them)
{
  std::string s    {repeated("dab cfd adc fbd ", 40000)};
  std::size_t want {0};
  for (std::size_t at {2000}; at + 3U < s.size(); at += 32U) {
    s.replace(at, 3, "cQz");
    ++want;
  }
  const real::regex re {"(?i)cqz|dqz|fqz"};
  std::size_t       n  {0};
  for (const auto& m : re.find_iter(s)) {
    static_cast<void>(m);
    ++n;
    real::detail::alternation_nibbles_disabled() = true;
  }
  real::detail::alternation_nibbles_disabled() = false;
  EXPECT_EQ(n, want);
}

// With the fingerprint off, a plan without pairs has no block filter: the alternation keeps nothing from the
// automaton's gate, which is asked as before.
TEST(alternation_without_pairs_leaves_the_automaton_its_gate)
{
  const real::regex re {"(?i)cqz|dqz|fqz|bqz"};
  const std::string s  {repeated("dab cfd adc fbd bqa ", 20000)};
  real::detail::alternation_nibbles_disabled() = true;
  real::detail::ac_density_last_verdict().store(real::detail::ac_verdict::not_consulted);
  EXPECT_EQ(re.count_matches(s), 0U);
  real::detail::alternation_nibbles_disabled() = false;
  EXPECT(real::detail::ac_density_last_verdict().load() != real::detail::ac_verdict::not_consulted);
}

namespace {
  //! \brief One case-folded spelling of \p c, drawn by \p next: `i`, `s` and `k` fold to non-ASCII code points
  //!        (`İ ı`, `ſ`, the Kelvin sign), the other letters to their other case.
  template <typename Next>
  std::string folded(char  c,
                     Next& next)
  {
    switch (c) {
      case 'i': {
          const std::array<const char*, 4> v {"i", "I", "\xC4\xB0", "\xC4\xB1"};
          return v[next() % v.size()];
        }
      case 'k': {
          const std::array<const char*, 3> v {"k", "K", "\xE2\x84\xAA"};
          return v[next() % v.size()];
        }
      case 's': {
          const std::array<const char*, 3> v {"s", "S", "\xC5\xBF"};
          return v[next() % v.size()];
        }
      default:
        return std::string(1, next() % 2U == 0U ? static_cast<char>(c - 'a' + 'A') : c);
    }
  }
} // namespace

// An alternation whose branches hold an `i`, `s` or `k` under case folding is not a fixed alternation: those
// fold to non-ASCII code points. The fingerprint of their UTF-8 variants picks the candidates the anchored walk
// confirms; random ones over subjects dense in their first bytes, with folded matches and stray lead and
// continuation bytes, answer as the first-byte scan and the VM.
TEST(alternation_variants_answer_as_the_first_bytes_and_the_vm)
{
  std::uint32_t state {0x510E527FU};
  const auto    next  {[&state] {
                         state ^= state << 13U;
                         state ^= state >> 17U;
                         state ^= state << 5U;
                         return state;
                       }};
  const std::string                  letters  {"iskaefnqz"};
  const std::array<const char*, 20>  alphabet {"i", "I", "s", "S", "k", "K", "a", "e", "n", "f", " ", "q", "z",
                                               "\xC4\xB0", "\xC4\xB1", "\xC5\xBF", "\xE2\x84\xAA", "\xC3\xA9", "\xC4",
                                               "\xB0"};
  std::size_t armed {0};
  for (int round {0}; round < 200; ++round) {
    const std::size_t        branches {2U + (next() % 12U)};
    std::vector<std::string> body;
    std::string              pattern  {"(?i)"};
    for (std::size_t b {0}; b < branches; ++b) {
      std::string       branch;
      const std::size_t width {2U + (next() % 4U)};
      while (branch.size() < width) {
        branch += letters[next() % letters.size()];
      }
      if (b > 0) {
        pattern += '|';
      }
      pattern += branch;
      body.push_back(branch);
    }
    std::string       s;
    const std::size_t size {4200U + (next() % 6000U)};
    while (s.size() < size) {
      s += alphabet[next() % (next() % 8U == 0U ? 20U : 13U)];
    }
    const std::size_t planted {next() % 8U};
    for (std::size_t k {0}; k < planted; ++k) {
      std::string x;
      for (const char c : body[next() % body.size()]) {
        x += folded(c, next);
      }
      const std::size_t at {k == 0 ? s.size() - x.size() - (next() % 40U) : next() % (s.size() - x.size())};
      s.replace(at, x.size(), x);
    }
    const real::regex re {pattern};
    real::detail::tally(real::detail::counter::alternation_variant_scans) = 0;
    const span_list got  {spans_of(re, s)};
    armed                                     += real::detail::tally(real::detail::counter::alternation_variant_scans).load() != 0U ? 1U : 0U;
    real::detail::alternation_pairs_disabled() = true;
    const span_list first_bytes {spans_of(re, s)};
    real::detail::alternation_pairs_disabled() = false;
    real::detail::lazy_dfa_route_disabled()    = true;
    const span_list vm {spans_of(re, s)};
    real::detail::lazy_dfa_route_disabled() = false;
    if (got != first_bytes || got != vm) {
      std::printf("/%s/ on %zu bytes: %zu, first bytes %zu, vm %zu\n", pattern.c_str(), s.size(), got.size(),
                  first_bytes.size(), vm.size());
    }
    EXPECT(got == first_bytes);
    EXPECT(got == vm);
    EXPECT_EQ(re.count_matches(s), vm.size());
  }
  std::printf("  variants: %zu of 200 cases took the fingerprint\n", armed);
  EXPECT(fingerprint_here() ? armed >= 150U : armed == 0U);
}

// A match spelled only with a non-ASCII fold is found: `İnfo` (U+0130 then `nfo`) matches `(?i)info`, whose
// fingerprint admits U+0130's lead byte at the start and its continuation where `n` would sit.
TEST(alternation_variants_find_a_non_ascii_fold)
{
  const real::regex re {"(?i)info|error|warn"};
  std::string       s  {repeated("dab cfd adc fbd ewq ", 20000)};
  s                                      += "\xC4\xB0nfo and \xC4\xB1NFO and w\xC4\x81rn";
  real::detail::lazy_dfa_route_disabled() = true;
  const std::size_t want {re.count_matches(s)};
  real::detail::lazy_dfa_route_disabled() = false;
  EXPECT_EQ(want, 2U);
  real::detail::tally(real::detail::counter::alternation_variant_scans) = 0;
  EXPECT_EQ(re.count_matches(s), want);
  EXPECT(fingerprint_here() ? real::detail::tally(real::detail::counter::alternation_variant_scans).load() > 0U
                            : real::detail::tally(real::detail::counter::alternation_variant_scans).load() == 0U);
}

// A leading word boundary only narrows where a match starts, so the fingerprint is still a superset of the
// starts; and where it declines -- one branch, a short subject, a fixed alternation -- nothing is counted.
TEST(alternation_variants_take_a_leading_boundary_and_decline_the_rest)
{
  const std::string s       {repeated("dab cfd adc fbd ewq info fish ", 20000)};
  const real::regex bounded {R"((?i)\b(?:info|fish)\b)"};
  real::detail::lazy_dfa_route_disabled() = true;
  const std::size_t want    {bounded.count_matches(s)};
  real::detail::lazy_dfa_route_disabled() = false;
  real::detail::tally(real::detail::counter::alternation_variant_scans) = 0;
  EXPECT_EQ(bounded.count_matches(s), want);
  EXPECT(fingerprint_here() ? real::detail::tally(real::detail::counter::alternation_variant_scans).load() > 0U
                            : real::detail::tally(real::detail::counter::alternation_variant_scans).load() == 0U);
  for (const std::string& pattern : {std::string {"(?i)info"}, std::string {"(?a)(?i)info|fish"}}) {
    real::detail::tally(real::detail::counter::alternation_variant_scans) = 0;
    static_cast<void>(real::regex {pattern}.count_matches(s));
    EXPECT_EQ(real::detail::tally(real::detail::counter::alternation_variant_scans).load(), 0U);
  }
  real::detail::tally(real::detail::counter::alternation_variant_scans) = 0;
  static_cast<void>(real::regex {"(?i)info|fish"}.count_matches(s.substr(0, 3000)));
  EXPECT_EQ(real::detail::tally(real::detail::counter::alternation_variant_scans).load(), 0U);
}

// Copy-assigning a regex reuses its program's buffer when the sizes allow, so every identity key must be
// cleared: the alternation plan's was not, and an alternation assigned over another of the same compiled size
// searched with the previous one's fingerprint -- 2 matches of 2500. Both routes that read the plan: an
// alternation of at most eight first bytes, and one of more.
TEST(alternation_plan_follows_a_copy_assignment_that_reuses_the_program)
{
  std::string s;
  while (s.size() < 20000U) {
    s += "xyq aaa ";
  }
  const std::vector<std::pair<std::string, std::string>> pairs {
    {"abc|bcd|cde|def|efg|fgh|ghi|hij|ijk", "xyq|yqx|qxy|kqx|lqx|mqx|nqx|oqx|pqx"}, // wide: nine first bytes
    {"abc|bcd|cde", "xyq|yqx|qxy"}};                                                 // small set: three
  for (const auto& [first, second] : pairs) {
    real::regex       re     {first};
    const real::regex other  {second};
    const void* const buffer {re.raw_program().code.data()};
    EXPECT_EQ(re.count_matches(s), 0U); // builds the first pattern's plan
    re = other;
    if (re.raw_program().code.data() != buffer) {
      std::printf("  %s: the program buffer was not reused, the case is not reached\n", second.c_str());
    }
    EXPECT_EQ(re.count_matches(s), other.count_matches(s));
    EXPECT_EQ(re.count_matches(s), 2500U);
  }
}

// The wide route verifies a candidate only against the branches of the buckets its three bytes admit. Where fewer
// than three bytes remain, the scans leave the start to the first-byte table and every branch is tried: a
// two-byte branch matching in the subject's last two bytes is still found.
TEST(alternation_wide_finds_a_two_byte_branch_at_the_end)
{
  const real::regex re {"(?i)cqz|dqz|fqz|bqz|tqz|pq"};
  std::string       s  {repeated("dab cfd adc fbd tqb BqA ", 20000)};
  s += " cQz pq";
  real::detail::tally(real::detail::counter::alternation_wide_scans) = 0;
  const span_list got  {spans_of(re, s)};
  const auto      wide {real::detail::tally(real::detail::counter::alternation_wide_scans).load()};
  real::detail::alternation_pairs_disabled() = true;
  const span_list walk {spans_of(re, s)};
  real::detail::alternation_pairs_disabled() = false;
  EXPECT(got == walk);
  EXPECT_EQ(got.size(), 2U);
  EXPECT(fingerprint_here() ? wide > 0U : wide == 0U); // the route under test took the subject
}

// A walk over an alternation the wide route takes is batched: its filler runs the route's scan from each match's
// end instead of re-entering run() per match, so a walk with many matches fills several times, and finds what the
// walk without the fingerprint finds.
TEST(alternation_wide_walk_is_batched)
{
  const real::regex re {"the|and|for|with|not|but|some|just|more|here|words|all"};
  const std::string s  {repeated("the quick brown fox jumps over the lazy dog while the cat sleeps near the fire\n"
                                 "some ordinary prose without anything interesting in it at all, just words here\n",
                                 64000)};
  real::detail::tally(real::detail::counter::batch_fills) = 0;
  const span_list got   {spans_of(re, s)};
  const auto      fills {real::detail::tally(real::detail::counter::batch_fills).load()};
  real::detail::alternation_pairs_disabled() = true;
  const span_list walk  {spans_of(re, s)};
  real::detail::alternation_pairs_disabled() = false;
  EXPECT(got == walk);
  EXPECT(got.size() > 3000U);
  EXPECT(fingerprint_here() ? fills > 10U : true);
}
