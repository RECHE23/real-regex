//! The lazy DFA against the Pike VM on patterns that carry position assertions -- anchors and word
//! boundaries -- over subjects long enough to reach the DFA routes. Every mode, find_iter included, spans
//! and groups compared, against two references: the same regex with the DFA taken out by its knob, and the
//! plain Pike VM on the program with its hints blanked -- which no route reaches, where the knob only takes
//! the DFA out and leaves every other route in (one of them was answering wrong behind it).
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <iterator>
#include <vector>

namespace {

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

  std::string answers(const real::regex& re,
                      std::string_view   text)
  {
    std::string out   {describe(re.search(text)) + " | " + describe(re.search(text, 7)) + " | "
                       + describe(re.match(text, 3)) + " | " + describe(re.fullmatch(text)) + " |"};
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

  //! What the plain Pike VM answers for the single-attempt queries \ref answers makes: the program with its
  //! hints blanked, run directly, so no dispatch route takes part.
  std::string pure_answers(std::string_view pattern,
                           real::flags      fl,
                           std::string_view text)
  {
    auto program {real::detail::dynamic_storage::compile(pattern, fl)};
    program.program.hints = {};
    const real::detail::program_view pv {program.view()};
    const auto                       run {[&](std::size_t start, real::detail::run_mode mode) {
                                            // The generic state carries its own fallback DFA: taking the knob out keeps it off this reference.
                                            real::detail::lazy_dfa_route_disabled() = true;
                                            real::detail::pike_state state;
                                            std::vector<std::size_t> slots;
                                            real::detail::pike_vm    vm(pv, state);
                                            const bool matched {vm.run(text, start, mode, slots)};
                                            real::detail::lazy_dfa_route_disabled() = false;
                                            if (!matched) {
                                              return std::string {"none"};
                                            }
                                            std::string out;
                                            for (std::size_t g {0}; g + 1U < slots.size(); g += 2U) {
                                              out += std::to_string(slots[g]) + "," + std::to_string(slots[g + 1U]) + ";";
                                            }
                                            return out;
                                          }};
    return run(0, real::detail::run_mode::search) + " | " + run(7, real::detail::run_mode::search) + " | "
           + run(3, real::detail::run_mode::prefix) + " | " + run(0, real::detail::run_mode::full) + " |";
  }

  std::string vm_answers(const real::regex& re,
                         std::string_view   text)
  {
    real::detail::lazy_dfa_route_disabled() = true;
    std::string out {answers(re, text)};
    real::detail::lazy_dfa_route_disabled() = false;
    return out;
  }

  struct generator
  {
    std::uint64_t state {0xD1B54A32D192ED03ULL};

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

    std::string pattern()
    {
      static constexpr std::string_view assertions[] {"^", "$", "\\b", "\\B", "\\A", "\\Z", "(?m:^)", "(?m:$)"};
      static constexpr std::string_view atoms[]      {"[a-z]+", "\\d+", "x", "ab", "[a-z]", "\\w+", " ", "[^ ]+",
                                                      "(\\w+)", "(?:ab|a)", "\\d{2}", "e", ".", ".*", "\\s+", "x?",
                                                      "(^|x)", "(?:$|y)", "(?:\\b|z)", "[^\\n]*"};
      std::string out;
      for (std::uint32_t k {1U + (next() % 4U)}; k > 0; --k) {
        if (next() % 2U == 0U) {
          out += pick(assertions);
        }
        out += pick(atoms);
      }
      if (next() % 2U == 0U) {
        out += pick(assertions);
      }
      if (next() % 5U == 0U) {
        out.insert(0, "(?a)"); // ASCII word-ness: the boundaries a byte DFA can decide
      }
      return out;
    }

    real::flags flags()
    {
      static constexpr real::flags choices[] {real::flags::none, real::flags::none, real::flags::multiline,
                                              real::flags::bytes, real::flags::ascii, real::flags::dotall};
      return choices[next() % std::size(choices)];
    }

    std::string subject()
    {
      static constexpr std::string_view pieces[] {"abc ", "x", "12 ", "\n", "ab", "e ", " ", "word", "é", "_",
                                                  "99", "\n\n", "xyz", "\r\n", "y", "z"};
      std::string out;
      while (out.size() < 600U + (next() % 200U)) {
        out += pick(pieces);
      }
      if (next() % 3U == 0U) {
        out += '\n'; // a final newline: where Python's `$` also holds
      }
      return out;
    }
  };
} // namespace

TEST(dfa_with_assertions_agrees_with_the_vm)
{
  generator gen;
  int       compared {0};
  for (int round {0}; round < 3000; ++round) {
    const std::string          pattern {gen.pattern()};
    const real::flags          fl      {gen.flags()};
    std::optional<real::regex> re;
    try {
      re.emplace(pattern, fl);
    }
    catch (const real::regex_error&) {
      continue;
    }
    const std::string text   {gen.subject()};
    const std::string routed {answers(*re, text)};
    const std::string vm     {vm_answers(*re, text)};
    const std::string pure   {pure_answers(pattern, fl, text)};
    const std::string single {routed.substr(0, pure.size())}; // the four single-attempt queries
    if (routed != vm || single != pure) {
      std::printf("/%s/ on a %zu-byte subject:\n  dfa  %s\n  vm   %s\n  pure %s\n", pattern.c_str(), text.size(),
                  routed.substr(0, 300).c_str(), vm.substr(0, 300).c_str(), pure.c_str());
    }
    EXPECT_EQ(routed, vm);
    EXPECT_EQ(single, pure);
    ++compared;
  }
  EXPECT(compared >= 2500);
}

// A `\B` holds between the two bytes of `é` -- both are non-word bytes -- but no match starts inside a code
// point in text mode. The forward pass must not seed there; an empty match makes the difference visible, and
// the random subjects above reach that shape too rarely to count on.
TEST(dfa_with_assertions_starts_no_match_inside_a_code_point)
{
  for (const std::string_view pattern : {"(?a)\\B", "(?a)\\Bx?", "(?a)\\B\\w*"}) {
    for (const std::string_view unit : {"a\xC3\xA9", "\xC3\xA9" "a b"}) {
      std::string text;
      while (text.size() < 700U) {
        text += unit;
      }
      const real::regex re     {std::string {pattern}};
      const std::string routed {answers(re, text)};
      const std::string pure   {pure_answers(pattern, real::flags::none, text)};
      EXPECT_EQ(routed, vm_answers(re, text));
      EXPECT_EQ(routed.substr(0, pure.size()), pure); // the four single-attempt queries
    }
  }
}

// A program with assertions finds its matches in one forward pass and one reverse, never by an anchored walk
// from every candidate: that walk rescans each run the single pass crosses once, and costs no more than a
// constant factor, so no answer changes and no complexity test sees it. The reference here is the bare
// forward pass over the same text, built from the same program in this binary, so machine speed cancels.
//
// Measured on 200 KB (best of seven, 2026-09-26, arm64): both queries run at 1.00x the bare pass; the walk
// per candidate ran count_matches at 23.6x and 25.2x and search at 11.7x and 12.6x. The bound sits at 3x,
// three times the passing ratio and a quarter of the smallest failing one.
TEST(dfa_with_assertions_scans_once_not_once_per_candidate)
{
  using clock_type = std::chrono::steady_clock;
  constexpr double ratio_bound {3.0};
  std::string      text;
  while (text.size() < 200000U) {
    text += "the quick fox singing 123x and bringing 7x over 42 dogs ";
  }
  const auto best_ns {[](const auto& run) {
                        double best {-1.0};
                        for (int r {0}; r < 7; ++r) {
                          const auto   t0 {clock_type::now()};
                          run();
                          const double ns {std::chrono::duration<double, std::nano>(clock_type::now() - t0).count()};
                          best = (best < 0.0 || ns < best) ? ns : best;
                        }
                        return best;
                      }};
  for (const std::string_view pattern : {"[a-z ]*x$", "(?a)[a-z ]+\\b9"}) {
    const auto                   compiled {real::detail::dynamic_storage::compile(pattern, real::flags::none)};
    const auto                   pv       {compiled.view()};
    const auto                   bp       {real::detail::build_byte_program(pv, /*keep_assertions=*/ true)};
    real::detail::lazy_dfa       fwd(bp.code, bp.classes, real::detail::lazy_dfa::state_budget, nullptr, !bp.unicode_word,
                                     pv.byte_mode);
    const real::regex            re   {std::string {pattern}};
    std::size_t                  sink {0};
    EXPECT(fwd.eligible());
    const double pass                 {best_ns([&] {
                                                 fwd.begin_scan();
                                                 sink += fwd.forward_end(text, 0);
                                               })};
    const double counts {best_ns([&] { sink += re.count_matches(text); })};
    const double finds  {best_ns([&] { sink += re.search(text) ? 1U : 0U; })};
    std::printf("  %s: count_matches %.2fx, search %.2fx the bare forward pass\n", std::string {pattern}.c_str(),
                counts / pass, finds / pass);
    EXPECT(counts < ratio_bound * pass);
    EXPECT(finds < ratio_bound * pass);
    EXPECT(sink != 0U);
  }
}
