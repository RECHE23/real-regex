// real::dfa — a maximal-munch DFA over a set of patterns (opt-in, real/dfa.hpp).
//
// The spine is a differential: the DFA must reach the SAME maximal-munch decision
// (longest match; ties to the earliest rule; empty excluded) as running each
// pattern's Pike VM independently — checked over many random inputs. Plus targeted
// cases for the tie-break, empty exclusion, the assertion contract (dfa_error), and
// the accessors.
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/dfa.hpp"

namespace {

  //! \brief The reference: per-rule Pike maximal munch (longest, earliest on a tie,
  //!        empty excluded) — what real::dfa must reproduce in one pass.
  std::optional<real::dfa_match> pike_munch(const std::vector<real::regex>& pats,
                                            std::string_view                rest)
  {
    std::optional<real::dfa_match> best;
    for (std::size_t i = 0; i < pats.size(); ++i) {
      const auto matched {pats[i].match(rest)};
      if (matched && matched.end() > 0) {
        const std::size_t len {matched.end()};
        if (!best.has_value() || len > best->length) { // strictly greater ⇒ earliest rule wins a tie
          best = real::dfa_match {.rule_index = static_cast<std::uint32_t>(i), .length = len};
        }
      }
    }
    return best;
  }

  bool same(const std::optional<real::dfa_match>& a,
            const std::optional<real::dfa_match>& b)
  {
    if (a.has_value() != b.has_value()) {
      return false;
    }
    if (!a.has_value()) {
      return true;
    }
    return a->rule_index == b->rule_index && a->length == b->length;
  }

  //! \brief A small but representative SQL-ish grammar (keywords overlap ident on letters).
  std::vector<real::regex> sql_like()
  {
    std::vector<real::regex> pats;
    pats.emplace_back(R"([ \t\r\n]+)");              // 0 ws
    pats.emplace_back("select", real::flags::icase); // 1 keyword
    pats.emplace_back("from", real::flags::icase);   // 2 keyword
    pats.emplace_back("set", real::flags::icase);    // 3 keyword
    pats.emplace_back("[A-Za-z_][A-Za-z0-9_]*");     // 4 ident
    pats.emplace_back(R"([0-9]+(\.[0-9]+)?)");       // 5 number
    pats.emplace_back("'([^']|'')*'");               // 6 string
    pats.emplace_back("<>|<=|>=|[-+*/<>=]");         // 7 op
    return pats;
  }
} // namespace

// The differential — DFA vs per-rule Pike munch over random inputs.
TEST(dfa_matches_pike_munch_over_random_inputs)
{
  const std::vector<real::regex> pats {sql_like()};
  const real::dfa                d    {std::span<const real::regex>(pats)};

  const std::string alphabet          {" \tselectfromSET_abc123.'<>=+-"};
  // A fixed seed is deliberate: the differential must be reproducible (a failure
  // replays exactly). The cert/bugprone seed rules target security-sensitive RNG.
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed)
  std::mt19937                               rng      {0xD1CEU};
  std::uniform_int_distribution<std::size_t> len_d    {0, 24};
  std::uniform_int_distribution<std::size_t> sym_d    {0, alphabet.size() - 1};
  long                                       compared {0};
  for (int it = 0; it < 20000; ++it) {
    std::string       input;
    const std::size_t len {len_d(rng)};
    for (std::size_t i = 0; i < len; ++i) {
      input.push_back(alphabet[sym_d(rng)]);
    }
    // Compare the decision at every offset (a lexer munches from each cursor).
    for (std::size_t off = 0; off <= input.size(); ++off) {
      const std::string_view rest {std::string_view(input).substr(off)};
      if (!same(d.match(rest), pike_munch(pats, rest))) {
        EXPECT(false);
        return;
      }
      ++compared;
    }
  }
  EXPECT(compared > 100000); // the differential must be non-vacuous
}

// Equal-length matches resolve to the earliest pattern; a strictly longer one wins.
TEST(dfa_tiebreak_prefers_earliest_then_longest)
{
  std::vector<real::regex> pats;
  pats.emplace_back("if");                       // 0 keyword
  pats.emplace_back("[A-Za-z_][A-Za-z0-9_]*");   // 1 ident
  const real::dfa d {std::span<const real::regex>(pats)};

  const auto kw     {d.match("if")};             // both match len 2 → earliest (rule 0)
  EXPECT(kw.has_value());
  EXPECT_EQ(kw->rule_index, 0U);
  EXPECT_EQ(kw->length, std::size_t {2});

  const auto id {d.match("iffy")};               // ident len 4 > keyword len 2 → rule 1
  EXPECT(id.has_value());
  EXPECT_EQ(id->rule_index, 1U);
  EXPECT_EQ(id->length, std::size_t {4});
}

// A nullable pattern never wins with an empty match; a non-empty one still can.
TEST(dfa_excludes_empty_matches)
{
  std::vector<real::regex> pats;
  pats.emplace_back("a*");                   // 0 nullable
  pats.emplace_back("b");                    // 1
  const real::dfa d {std::span<const real::regex>(pats)};

  EXPECT(d.match("b").has_value());          // 'b' matches rule 1
  EXPECT_EQ(d.match("b")->rule_index, 1U);
  EXPECT_EQ(d.match("aaa")->rule_index, 0U); // 'a*' matches 3 (non-empty)
  EXPECT_EQ(d.match("aaa")->length, std::size_t {3});
  EXPECT(!d.match("x").has_value());         // 'a*' empty (excluded), 'b' no match → none
  EXPECT(!d.match("").has_value());
}

// No pattern matches ⇒ no result.
TEST(dfa_no_match_is_nullopt)
{
  std::vector<real::regex> pats;
  pats.emplace_back("abc");
  const real::dfa d {std::span<const real::regex>(pats)};
  EXPECT(!d.match("xyz").has_value());
  EXPECT(d.match("abcd").has_value());
  EXPECT_EQ(d.match("abcd")->length, std::size_t {3});
}

// The assertion contract: a leading ^ is a no-op (allowed); any other assertion throws.
TEST(dfa_assertion_contract)
{
  {
    std::vector<real::regex> ok;
    ok.emplace_back("^abc"); // leading text_start: a no-op under anchored scanning
    const real::dfa d {std::span<const real::regex>(ok)};
    EXPECT(d.match("abc").has_value());
    EXPECT_EQ(d.match("abc")->length, std::size_t {3});
  }
  EXPECT_THROWS(real::dfa {std::span<const real::regex>(std::vector<real::regex> {real::regex("abc$")})},
                real::dfa_error);                                                       // trailing $
  EXPECT_THROWS(real::dfa {std::span<const real::regex>(std::vector<real::regex> {real::regex(R"(\bword)")})},
                real::dfa_error);                                                       // \b
}

// A code-point class BUILDS here. It used to be refused outright ("no DFA can represent"), which was a
// limit of this entry point and not of the DFA: `dfa_flatten` now expands `klass_cp` through the same
// `build_byte_program` the lazy DFA has always used, so the deterministic UTF-8 trie recognising the class
// becomes ordinary byte transitions. Text-mode `\w`/`\d`/`\s`, Unicode properties, non-ASCII classes and
// case-folded ASCII classes are all constructible.
TEST(dfa_accepts_code_point_classes)
{
  const std::vector<std::string> pats {R"(\d+)", R"(\s+)", R"(\p{Greek}+)", R"((?i)[a-z]+)",
                                       R"([àéè]+)", R"((?i)[àé]+)", R"(\p{Han}+)"};
  for (const std::string& p : pats) {
    const std::vector<real::regex> one {real::regex(p)};
    const real::dfa                d   {std::span<const real::regex>(one)};
    EXPECT(d.state_count() > 0);
  }
}

// And it AGREES with the engine, which is the part that matters: for each pattern, the DFA finds an
// anchored non-empty match exactly when the engine does. Both are driven over the same subjects, ASCII and
// not, including bytes that are not valid UTF-8 at all.
TEST(dfa_code_point_classes_agree_with_the_engine)
{
  const std::vector<std::string> pats  {R"(\d+)", R"([a-z]+)", R"((?i)[a-z]+)", R"(\p{Greek}+)",
                                        R"([àéè]+)", R"((?i)[a-z]{3})", R"((?i)[a-z]{8})"};
  const std::vector<std::string> texts {"", "a", "abc", "ABC", "123", "  ", "café", "àéè", "ΑΒΓαβγ",
                                        "\u65e5\u672c\u8a9e", "root@localhost", "\u017f\u212a", "x\xC3", "\x80x"};
  for (const std::string& p : pats) {
    const real::regex              re  {p};
    const std::vector<real::regex> one {real::regex(p)};
    const real::dfa                d   {std::span<const real::regex>(one)};
    for (const std::string& t : texts) {
      const auto md              {d.match(t)};
      const auto mr              {re.search(t)};
      const bool engine_anchored {mr.matched() && mr.start(0) == 0 && mr.end(0) > 0};
      EXPECT_EQ(md.has_value(), engine_anchored);
    }
  }
}

TEST(dfa_state_cap_rejects_explosion)
{
  // Subset construction is 2^NFA in the worst case; the state cap turns that into a clean
  // dfa_error rather than a memory blow-up. A tiny cap keeps this fast (the production default,
  // max_dfa_states, is far larger and is exercised by every other DFA test).
  const real::regex                             rx("[ab]*a[ab][ab][ab][ab][ab]"); // ~2^6 DFA states
  const std::vector<real::detail::program_view> views {rx.raw_program()};
  EXPECT_THROWS(real::detail::dfa_build(views, 32), real::dfa_error);

  // The same pattern builds fine under the generous default cap; lexer-style DFAs are tiny and
  // are never rejected.
  const std::vector<real::regex> ok {real::regex("[ab]*a[ab][ab][ab][ab][ab]")};
  const real::dfa                d  {std::span<const real::regex>(ok)};
  EXPECT(d.match("aaaaaa").has_value());
}

// Accessors report a sane, minimized automaton.
TEST(dfa_accessors)
{
  const std::vector<real::regex> pats {sql_like()};
  const real::dfa                d    {std::span<const real::regex>(pats)};
  EXPECT_EQ(d.rule_count(), pats.size());
  EXPECT(d.state_count() > 1);          // at least dead + one live state
  EXPECT(d.class_count() > 0 && d.class_count() <= 256);
}

// The two constructors (programs vs regexes) build the same automaton.
TEST(dfa_program_view_and_regex_ctors_agree)
{
  const std::vector<real::regex>          pats {sql_like()};
  std::vector<real::detail::program_view> views;
  views.reserve(pats.size());
  for (const real::regex& pattern : pats) {
    views.push_back(pattern.raw_program());
  }
  const real::dfa via_regex {std::span<const real::regex>(pats)};
  const real::dfa via_views {std::span<const real::detail::program_view>(views)};
  EXPECT_EQ(via_regex.state_count(), via_views.state_count());
  EXPECT(same(via_regex.match("select * from t"), via_views.match("select * from t")));
}
