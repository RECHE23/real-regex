//! A literal alternation of many branches is factored into a trie in the byte program, so a DFA state holds one
//! pc per live node instead of one per branch. These pin that the trie keeps leftmost-first priority -- a branch
//! that a later one extends, a later branch that an earlier one prefixes, an earlier branch that ends inside a
//! later one's path -- that it leaves smaller alternations' programs alone, and that its states stay small.
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  using span_list = std::vector<std::pair<std::size_t, std::size_t>>;
  using real::detail::build_byte_program;
  using real::detail::dynamic_storage;
  using real::detail::lazy_dfa;

  span_list spans_of(const real::regex& re,
                     const std::string& s)
  {
    span_list out;
    for (const auto& m : re.find_iter(s)) {
      out.emplace_back(m.start(), m.end());
    }
    return out;
  }

  //! \brief The spans of \p pattern on \p s from the VM alone.
  span_list vm_spans(const std::string& pattern,
                     const std::string& s)
  {
    real::detail::lazy_dfa_route_disabled()          = true;
    real::detail::bounded_backtrack_route_disabled() = true;
    const real::regex vm  {pattern};
    span_list         out {spans_of(vm, s)};
    real::detail::lazy_dfa_route_disabled()          = false;
    real::detail::bounded_backtrack_route_disabled() = false;
    return out;
  }

  struct generator
  {
    std::uint32_t state {0x2545F491U};

    std::uint32_t next()
    {
      state ^= state << 13U;
      state ^= state >> 17U;
      state ^= state << 5U;
      return state;
    }

    //! \brief A word over a small alphabet, so branches share prefixes and prefix one another.
    std::string word(std::size_t min_width,
                     std::size_t spread)
    {
      std::string       w;
      const std::size_t width {min_width + (next() % spread)};
      while (w.size() < width) {
        w += static_cast<char>('a' + (next() % 4U));
      }
      return w;
    }
  };

  //! \brief `(?:w1|…|wn)` from \p words.
  std::string group(const std::vector<std::string>& words)
  {
    std::string out {"(?:"};
    for (std::size_t i {0}; i < words.size(); ++i) {
      out += i == 0U ? "" : "|";
      out += words[i];
    }
    return out + ")";
  }

  //! \brief \p n fillers no witness subject contains.
  std::vector<std::string> fillers(std::size_t n)
  {
    std::vector<std::string> out;
    for (std::size_t i {0}; i < n; ++i) {
      out.push_back("q" + std::to_string(i) + "z");
    }
    return out;
  }

  //! \brief The subject repeated past the lazy DFA's floor, so the route that reads the trie answers.
  std::string long_subject(const std::string& unit)
  {
    std::string s;
    while (s.size() < 1024U) {
      s += unit;
    }
    return s;
  }
} // namespace

// Leftmost-first through the trie: an earlier branch that ends inside a later one's path wins at its start, a
// later branch its predecessor prefixes loses to it, and branches that share a path keep their order.
TEST(alternation_trie_keeps_leftmost_first)
{
  const std::vector<std::pair<std::vector<std::string>, std::string>> cases {
    {{"abcx", "ab", "abcy"}, "abcy "}, // `ab` ends inside `abcy`'s path: it wins at 0
    {{"ab", "abc"}, "abc "},           // `abc` extends `ab`, which comes first
    {{"abc", "ab"}, "abc "},           // the longer one first
    {{"b", "ab", "a"}, "ab "},         // at 0 `ab` beats `a`
    {{"ab", "ab", "a"}, "ab "}};       // a repeated branch
  for (const auto& [branches, unit] : cases) {
    std::vector<std::string>       words {branches};
    const std::vector<std::string> pad   {fillers(real::detail::alternation_trie_min_branches)};
    words.insert(words.end(), pad.begin(), pad.end());
    const std::string pattern            {group(words) + " "};
    const std::string s                  {long_subject(unit)};
    const real::regex re                 {pattern};
    EXPECT(spans_of(re, s) == vm_spans(pattern, s));
  }
}

// Random alternations past the threshold -- words over four letters, so they share prefixes and prefix one
// another -- followed by a suffix or nested in a pattern, answer as the flat byte program and as the VM alone.
TEST(alternation_trie_answers_as_the_flat_program)
{
  generator   g;
  std::size_t cases {0};
  std::size_t wrong {0};
  for (int round {0}; round < 80; ++round) {
    std::vector<std::string> words;
    const std::size_t        n {real::detail::alternation_trie_min_branches + (g.next() % 40U)};
    while (words.size() < n) {
      words.push_back(g.word(1, 5));
    }
    const char* const shapes[] {"%s[0-9]", "x?%s", "%s+z", "(%s)(%s)?", "%s|dd"};
    std::string       pattern  {shapes[g.next() % 5U]};
    const std::string body     {group(words)};
    for (std::size_t at {pattern.find("%s")}; at != std::string::npos; at = pattern.find("%s")) {
      pattern.replace(at, 2, body);
    }
    std::string s;
    while (s.size() < 900U) {
      const std::uint32_t r {g.next() % 8U};
      if (r < 5U) {
        s += static_cast<char>('a' + (g.next() % 4U));
      }
      else {
        s += "7z "[r - 5U];
      }
    }
    const real::regex re      {pattern};
    const span_list   got     {spans_of(re, s)};
    real::detail::alternation_trie_disabled() = true;
    const real::regex flat_re {pattern};
    const span_list   flat    {spans_of(flat_re, s)};
    real::detail::alternation_trie_disabled() = false;
    const span_list want      {vm_spans(pattern, s)};
    if (got != want || flat != want) {
      if (wrong < 5U) {
        std::printf("/%.60s/: trie %zu, flat %zu, VM %zu spans\n", pattern.c_str(), got.size(), flat.size(), want.size());
      }
      ++wrong;
    }
    ++cases;
  }
  EXPECT_EQ(cases, 80U);
  EXPECT_EQ(wrong, 0U);
}

// Up to 63 branches the byte program is the flat one every smaller alternation has; from 64, the trie's.
TEST(alternation_trie_starts_at_64_branches)
{
  generator                g;
  std::vector<std::string> words;
  while (words.size() < 63U) {
    words.push_back(g.word(6, 4));
  }
  const auto below      {dynamic_storage::compile(group(words) + "[0-9]", real::flags::none)};
  words.push_back(g.word(6, 4));
  const auto at         {dynamic_storage::compile(group(words) + "[0-9]", real::flags::none)};
  const auto bp_below   {build_byte_program(below.program.view())};
  const auto bp_at      {build_byte_program(at.program.view())};
  real::detail::alternation_trie_disabled() = true;
  const auto flat_below {build_byte_program(below.program.view())};
  const auto flat_at    {build_byte_program(at.program.view())};
  real::detail::alternation_trie_disabled() = false;
  EXPECT_EQ(bp_below.code.size(), flat_below.code.size());
  EXPECT(bp_at.code.size() < flat_at.code.size()); // four letters: the branches share their first bytes
}

// The point of the trie: over 1 000 words its states stay small, and 5 000 words search without a flush where
// the flat program flushed until it quit.
TEST(alternation_trie_keeps_states_small)
{
  generator g;
  for (const std::size_t n : {std::size_t {1000}, std::size_t {5000}}) {
    std::vector<std::string> words;
    while (words.size() < n) {
      std::string w;
      while (w.size() < 16U) {
        w += static_cast<char>('a' + (g.next() % 26U));
      }
      words.push_back(std::move(w));
    }
    const auto  st {dynamic_storage::compile(group(words) + "[0-9]", real::flags::none)};
    std::string text;
    while (text.size() < 32768U) {
      text += static_cast<char>('a' + (g.next() % 26U));
    }
    const auto bp   {build_byte_program(st.program.view())};
    lazy_dfa   trie {bp.code, bp.classes};
    EXPECT_EQ(trie.forward_end(text), real::npos);
    real::detail::alternation_trie_disabled() = true;
    const auto flat_bp {build_byte_program(st.program.view())};
    real::detail::alternation_trie_disabled() = false;
    lazy_dfa flat      {flat_bp.code, flat_bp.classes};
    EXPECT_EQ(flat.forward_end(text), real::npos);
    EXPECT_EQ(trie.stats().flushes, 0U);
    EXPECT(trie.bytes() * 5U < flat.bytes() + (flat.stats().flushes * real::detail::lazy_dfa_default_byte_budget));
  }
}
