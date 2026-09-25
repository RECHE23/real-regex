//! Bounded backtracker: the general loop's answer on a small subject, which must be the Pike VM's --
//! the span, every group, and which of several candidate matches wins. Each test compares the route
//! against the VM with the route disabled, in one binary.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

using real::detail::dynamic_storage;

namespace {

  //! Every span of a match (npos for a group that did not take part), as one comparable string.
  template <typename Match>
  std::string describe(const Match& m)
  {
    if (!m) {
      return "none";
    }
    std::string out;
    for (std::size_t g {0}; g < m.size(); ++g) {
      out += std::to_string(m.start(g)) + "," + std::to_string(m.end(g)) + ";";
    }
    return out;
  }

  //! What \p re answers on \p text in each mode, find_iter included (its empty-match rule reaches the
  //! route through `forbid_empty_until`). A subject of n bytes holds at most n + 1 matches; the walk
  //! stops past that, so a broken empty-match rule reads as a wrong answer rather than a hang.
  std::string answers(const real::regex& re,
                      std::string_view   text)
  {
    std::string out   {describe(re.search(text)) + " | " + describe(re.match(text)) + " | "
                       + describe(re.fullmatch(text)) + " |"};
    std::size_t found {0};
    for (const auto& m : re.find_iter(text)) {
      if (++found > text.size() + 1U) {
        out += " (runaway)";
        break;
      }
      out += " " + describe(m);
    }
    return out;
  }

  //! \p re's answers with the route disabled -- the VM's.
  std::string vm_answers(const real::regex& re,
                         std::string_view   text)
  {
    real::detail::bounded_backtrack_route_disabled() = true;
    std::string out {answers(re, text)};
    real::detail::bounded_backtrack_route_disabled() = false;
    return out;
  }

  //! A small deterministic generator (xorshift), so the generated corpus is the same on every run and
  //! every compiler -- which is why no expression below draws from it twice.
  struct generator
  {
    std::uint64_t state {0x9E3779B97F4A7C15ULL};

    std::uint32_t next()
    {
      state ^= state << 13U;
      state ^= state >> 7U;
      state ^= state << 17U;
      return static_cast<std::uint32_t>(state >> 32U);
    }

    template <std::size_t N>
    std::string_view pick(const std::string_view (&items)[N])
    {
      return items[next() % N];
    }

    std::string atom(int depth)
    {
      static constexpr std::string_view leaves[]      {"a",  "b",   "c",  ".",   "[ab]", "\\w", "\\d",   "\\s", "\\b",
                                                       "\\B", "^",  "$",  "é",   "[^a]", "\\W", "(?:)", "x",   "[à-ü]"};
      static constexpr std::string_view quantifiers[] {"",   "",     "",   "*",  "+",    "?",  "*?", "+?",
                                                       "??", "{2}", "{1,3}", "{0,2}?", "*+", "++", "?+", "{2,}"};
      static constexpr std::string_view openers[]     {"(", "(?:", "(?>"};
      std::string                       out;
      if (depth < 3 && next() % 3 == 0) {
        out  = pick(openers); // drawn before the body: the operands of `+` are unsequenced
        out += expression(depth + 1) + ")";
      }
      else {
        out = pick(leaves);
      }
      return out + std::string(pick(quantifiers));
    }

    std::string expression(int depth)
    {
      std::string out {atom(depth)};
      for (std::uint32_t k {next() % 3}; k > 0; --k) {
        out += atom(depth);
      }
      if (next() % 4 == 0) {
        out += "|" + expression(depth + 1);
      }
      return out;
    }

    std::string subject()
    {
      static constexpr std::string_view pieces[] {"a", "b", "c", "x", " ", "1", "é", "ü", "\n", "ab", "\xC3", "_"};
      std::string                       out;
      for (std::uint32_t k {next() % 10}; k > 0; --k) {
        out += pick(pieces);
      }
      return out;
    }
  };
} // namespace

// Generated patterns over every opcode the route walks -- greedy, lazy and possessive quantifiers,
// atomic groups, captures, empty alternatives, assertions, byte and code-point classes -- on subjects
// mixing ASCII, two-byte code points, a stray lead byte and newlines.
TEST(bounded_backtrack_agrees_with_the_vm_on_generated_patterns)
{
  generator gen;
  int       compared {0};
  for (int round {0}; round < 1500; ++round) {
    const std::string          pattern {gen.expression(0)};
    std::optional<real::regex> re;
    try {
      re.emplace(pattern, gen.next() % 5 == 0 ? real::flags::ascii : real::flags::none);
    }
    catch (const real::regex_error&) {
      continue; // a generated pattern the parser refuses (a quantified assertion, say) has no answer to compare
    }
    for (int k {0}; k < 8; ++k) {
      const std::string text   {gen.subject()};
      const std::string routed {answers(*re, text)};
      const std::string vm     {vm_answers(*re, text)};
      if (routed != vm) {
        std::printf("/%s/ on \"%s\":\n  backtracker %s\n  vm          %s\n", pattern.c_str(), text.c_str(),
                    routed.c_str(), vm.c_str());
      }
      EXPECT_EQ(routed, vm);
      ++compared;
    }
  }
  EXPECT(compared >= 3000); // most generated patterns compile; far fewer would mean the generator broke
}

// Past its inline capacity the pending-branch stack spills to the heap: every iteration of `(a?)`
// leaves a split's other branch and two slot restores pending, so three hundred of them hold nine
// hundred jobs at once, with the subject still inside the budget.
TEST(bounded_backtrack_spills_its_pending_branches)
{
  std::string pattern;
  for (int k {0}; k < 300; ++k) {
    pattern += "(a?)";
  }
  pattern += "b";
  const real::regex re    {pattern};
  const std::size_t width {dynamic_storage::compile(pattern, real::flags::none).program.code.size()};
  for (const std::string_view text : {"", "a", "aab", "aaaab", "ab", "b"}) {
    EXPECT((text.size() + 1U) * width <= real::detail::bounded_backtrack_bits); // the route does take it
    EXPECT_EQ(answers(re, text), vm_answers(re, text));
  }
}

// Either side of the budget: the last subject length the route takes and the first it declines give
// the VM's answer, whichever loop ran.
TEST(bounded_backtrack_budget_edge)
{
  const std::string_view pattern {"(\\w+)@(\\w+)"};
  const real::regex      re      {pattern};
  const std::size_t      width   {dynamic_storage::compile(pattern, real::flags::none).program.code.size()};
  const std::size_t      last    {(real::detail::bounded_backtrack_bits / width) - 1U}; // the longest subject taken
  for (const std::size_t n : {last - 1U, last, last + 1U, last + 2U}) {
    std::string text(n - 3U, 'x');
    text += "@yz";
    EXPECT_EQ(answers(re, text), vm_answers(re, text));
    std::string miss(n, 'x');
    EXPECT_EQ(answers(re, miss), vm_answers(re, miss));
  }
}

// The hint admits a pattern without lookarounds and with at most sixteen groups, and nothing else.
TEST(bounded_backtrack_hint)
{
  const auto hint {[](const std::string& pattern) {
                     return dynamic_storage::compile(pattern, real::flags::none).program.hints.bounded_backtrack != 0U;
                   }};
  EXPECT(hint("(\\w+)@(\\w+)"));
  EXPECT(!hint("a(?=b)"));
  EXPECT(!hint("(?<=a)b"));
  std::string groups;
  for (int k {0}; k < 16; ++k) {
    groups += "(a)";
  }
  EXPECT(hint(groups));
  EXPECT(!hint(groups + "(a)"));
}
