//! The lazy DFA against the Pike VM on patterns that carry position assertions -- anchors and word
//! boundaries -- over subjects long enough to reach the DFA routes. Every mode, find_iter included, spans
//! and groups compared, against two references: the same regex with the DFA taken out by its knob, and the
//! plain Pike VM on the program with its hints blanked -- which no route reaches, where the knob only takes
//! the DFA out and leaves every other route in (one of them was answering wrong behind it).
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <array>
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
    for (const std::string_view unit : {"a\xC3\xA9", "\303\251a b"}) {
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

// A program with assertions walks from candidates like any other, and gives way to one forward pass and one
// reverse when those walks reread the text: inside a run that no match ends, a walk per candidate rescans what
// the single pass crosses once. That costs a constant factor, so no answer changes and no complexity test sees
// it. The reference here is the bare forward pass over the same text, built from the same program in this
// binary, so machine speed cancels.
//
// Measured on 200 KB (best of seven, 2026-09-26, arm64): both queries run at about 1.0x the bare pass; walks
// kept to the end ran count_matches at 23.6x and 25.2x and search at 11.7x and 12.6x. The bound sits at 3x,
// three times the passing ratio and a quarter of the smallest failing one.
TEST(dfa_with_assertions_scans_once_not_once_per_candidate)
{
  using clock_type = std::chrono::steady_clock;
  constexpr double ratio_bound {3.0};
  std::string      text;
  while (text.size() < 200000U) {
    text += "the quick fox singing 123x and bringing 7x over 42 dogs ";
  }
  // Best of seven rounds, the runs alternating within each round: a clock that speeds up part-way through (a
  // shared runner leaving a quiet period) then weighs on every run alike, not on whichever was timed first.
  const auto best_ns {[](const auto&... runs) {
                        std::array<double, sizeof...(runs)> best {};
                        best.fill(-1.0);
                        const auto one {[](const auto& run, double& slot) {
                                          const auto   t0 {clock_type::now()};
                                          run();
                                          const double ns {std::chrono::duration<double, std::nano>(clock_type::now() - t0).count()};
                                          slot = (slot < 0.0 || ns < slot) ? ns : slot;
                                        }};
                        for (int r {0}; r < 7; ++r) {
                          std::size_t k {0};
                          (one(runs, best[k++]), ...);
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
    const auto times                  {best_ns([&] {
                                                 fwd.begin_scan();
                                                 sink += fwd.forward_end(text, 0);
                                               },
                                               [&] { sink += re.count_matches(text); }, [&] { sink += re.search(text) ? 1U : 0U; })};
    const double pass   {times[0]};
    const double counts {times[1]};
    const double finds  {times[2]};
    std::printf("  %s: count_matches %.2fx, search %.2fx the bare forward pass\n", std::string {pattern}.c_str(),
                counts / pass, finds / pass);
    EXPECT(counts < ratio_bound * pass);
    EXPECT(finds < ratio_bound * pass);
    EXPECT(sink != 0U);
  }
}

// Unicode word boundaries -- the default in text mode -- ride in the DFAs as ASCII ones and quit where one
// must be decided next to a non-ASCII byte: there only the code point tells, and the VM answers. Every route
// that reads the search DFAs must honour the quit: the single match, the walks from candidates, the forward
// pass and the reverse, the span filler behind count_matches and find_iter, and the inner-literal confirm.
// The subjects put a non-ASCII byte first, last, in the middle, far from any word and against one, where the
// Unicode verdict differs from the ASCII one (`é` is a word character: no boundary inside `café`).
TEST(dfa_unicode_word_boundaries_quit_next_to_non_ascii)
{
  static constexpr std::string_view patterns[] {R"(\b\w+\b)",     R"(\b[a-z]+ing\b)", R"(\bfox\b|\bdog\b)", R"(\w+\b)",
                                                R"(\B\w\B)",      R"(\<\w+\>)",       R"(\b\w+ing\b)",      R"((\w+)\b)",
                                                R"(\d+\b)",       R"(x\b)",           R"(\b\w)",            R"(\w\b\W)",
                                                R"(\bcaf\w*\b)",  R"(é\b)",           R"(\b[a-z]+ \d+\b)",  R"(\S+ing\b)"};
  static constexpr std::string_view inserts[] {"\xC3\xA9", "\xE6\x97\xA5\xE6\x9C\xAC", "\xE2\x80\x99", "caf\xC3\xA9", "a\xC3\xA9x",
                                               "\303\251a", "fox\xC3\xA9", "\303\251dog"};
  std::string base;
  while (base.size() < 700U) {
    base += "the quick fox singing 123x and bringing 7x over 42 dogs ";
  }
  int compared {0};
  for (const std::string_view pattern : patterns) {
    const real::regex re {std::string {pattern}};
    for (const std::string_view insert : inserts) {
      for (const std::size_t at : {std::size_t {0}, std::size_t {1}, std::size_t {5}, std::size_t {23}, base.size() / 2U,
                                   base.size() - 3U, base.size()}) {
        std::string text         {base};
        text.insert(at, insert);
        const std::string routed {answers(re, text)};
        const std::string pure   {pure_answers(pattern, real::flags::none, text)};
        EXPECT_EQ(routed, vm_answers(re, text));
        EXPECT_EQ(routed.substr(0, pure.size()), pure);
        ++compared;
      }
    }
    // All ASCII: nothing quits, and the DFA answers alone.
    const std::string pure   {pure_answers(pattern, real::flags::none, base)};
    const std::string routed {answers(re, base)};
    EXPECT_EQ(routed, vm_answers(re, base));
    EXPECT_EQ(routed.substr(0, pure.size()), pure);
  }
  EXPECT(compared > 800);
}

// The same, on random patterns built from word boundaries, word classes and `é`, over subjects that mix ASCII
// words with non-ASCII bytes: every enumerating query against the same regex with the DFAs taken out by their
// knob. This seed reaches a confirm that ignores a quit and a `\<` decided on one side only, which the fixed
// cases above do not.
TEST(dfa_unicode_word_boundaries_random)
{
  std::uint64_t state {0x9E3779B97F4A7C14ULL};
  const auto    next  {[&state] {
                         state ^= state << 13U;
                         state ^= state >> 7U;
                         state ^= state << 17U;
                         return static_cast<std::uint32_t>(state >> 32U);
                       }};
  static constexpr std::string_view assertions[] {"\\b", "\\B", "\\<", "\\>"};
  static constexpr std::string_view atoms[]      {"\\w+", "\\w*", "\\w", "[a-z]+", "\xC3\xA9", "x", "s", "ing", "\\W", ".",
                                                  "\\d+", " ", "(\\w+)", "caf", "\\S+", "(?:a|\xC3\xA9)"};
  static constexpr std::string_view pieces[]     {"a", "\xC3\xA9", "\xE2\x80\x99", "caf", "ing", "s", " ", "x",
                                                  "\xE6\x97\xA5", "\n", "1", "_", "sing", " the "};
  for (int round {0}; round < 4000; ++round) {
    std::string pattern;
    for (std::uint32_t k {1U + (next() % 4U)}; k > 0; --k) {
      if (next() % 2U != 0U) {
        pattern += assertions[next() % std::size(assertions)];
      }
      pattern += atoms[next() % std::size(atoms)];
    }
    if (next() % 2U != 0U) {
      pattern += assertions[next() % std::size(assertions)];
    }
    std::string text;
    while (text.size() < 600U + (next() % 300U)) {
      text += pieces[next() % std::size(pieces)];
    }
    const real::regex re      {pattern};
    const std::string routed  {answers(re, text) + " #" + std::to_string(re.count_matches(text))};
    real::detail::lazy_dfa_route_disabled() = true;
    const std::size_t counted {re.count_matches(text)};
    real::detail::lazy_dfa_route_disabled() = false;
    const std::string vm      {vm_answers(re, text) + " #" + std::to_string(counted)};
    if (routed != vm) {
      std::printf("/%s/ on a %zu-byte subject:\n  dfa  %s\n  vm   %s\n", pattern.c_str(), text.size(),
                  routed.substr(0, 200).c_str(), vm.substr(0, 200).c_str());
    }
    EXPECT_EQ(routed, vm);
  }
}

// On an all-ASCII subject no Unicode word boundary quits, so the default text mode takes the DFAs as `(?a)`
// does: the alphabet keeps non-ASCII bytes in classes of their own. Were a punctuation byte to share a class
// with them, every boundary next to it would quit and hand its search to the VM (3.6x slower on 200 KB,
// arm64). The quits are counted rather than timed: a clock on a shared CI runner read 2.84x one day for the
// same build. And the count is shown to move where a boundary does meet a non-ASCII byte.
TEST(dfa_unicode_word_boundaries_never_quit_on_ascii)
{
  std::string text;
  while (text.size() < 200000U) {
    text += "the quick fox singing 123x and bringing 7x over 42 dogs ";
  }
  const real::regex unicode   {R"(\bfox\b|\bdog\b)"};
  const real::regex ascii     {R"((?a)\bfox\b|\bdog\b)"};
  real::detail::dfa_quits() = 0;
  const std::size_t n_unicode {unicode.count_matches(text)};
  EXPECT_EQ(real::detail::dfa_quits().load(), 0U);
  EXPECT_EQ(n_unicode, ascii.count_matches(text));
  EXPECT(n_unicode > 0U);

  // The witness, on the same route and scale: a curly apostrophe after every `fox` puts a boundary next to a
  // non-ASCII byte throughout. (One apostrophe at the tail of the subject would not do: the last stretch is
  // served by another route, and the count would stay 0 there for a reason that is not this property.)
  std::string curly;
  while (curly.size() < 200000U) {
    curly += "the quick fox\u2019s singing 123x and bringing 7x over 42 dogs ";
  }
  real::detail::dfa_quits() = 0;
  EXPECT_EQ(unicode.count_matches(curly), ascii.count_matches(curly));
  EXPECT(real::detail::dfa_quits().load() > 0U);
}

// A program of saves, atoms and greedy `atom+` loops has its groups read by one walk that takes every loop as
// far as it goes (pike_vm::match_run_shape), over the window the DFAs found. Where that walk is not the
// VM's path it cannot end at the window's end -- `(\w+)(\d+)` must give digits back, `(a+)(a+)` must leave
// one `a` -- and the VM decides. Every group of every query against the plain VM.
TEST(dfa_run_shape_groups_match_the_vm)
{
  static constexpr std::string_view patterns[] {R"((\w+)\s+(\w+))", R"((\w+)(\d+))",   R"((a+)(a+))",      R"((\d+)\s+(\w+))",
                                                R"(([a-z]+)(\d+))", "(\xC3\xA9+)(\\w+)", R"((\S+)\s+(\S+))", R"(x(\w+)y)",
                                                R"((\w)(\w+))",     R"((\w+)(\w))",     R"(([ab]+)b(a+))",  R"((\p{L}+) (\d+))",
                                                R"((\w+)\s*=\s*(\w+))", R"((\w*)x(\d*))", R"((a*)(a*))",   R"((\w+?)x)",
                                                R"((\w+)\s+=\s+(\w+))", R"(([a-z]+)\s+(\d+))", R"(=(\s*)(\w*))", R"((\d*)(\d+))",
                                                R"((\s)+(\w)+)", R"((\w)+\s+(\w)+)", R"((a|e|i|o|u)+)", R"(((\w))+)",
                                                R"((\s\w)+)",     R"((\w\s)+x)",      R"((\w\s)+\w*)",   R"((\w)(\s\w)+)"};
  static constexpr std::string_view units[]    {"the quick fox 42 dogs ", "aaa aa a9 99x ", "x\xC3\xA9\xC3\xA9t\xC3\xA9 12 abba y ",
                                                "xabcy x9y aab ba key = val n=7  x  =  y "};
  int compared                                 {0};
  for (const std::string_view pattern : patterns) {
    const real::regex re {std::string {pattern}};
    for (const std::string_view unit : units) {
      std::string text;
      while (text.size() < 700U) {
        text += unit;
      }
      const std::string routed {answers(re, text)};
      const std::string pure   {pure_answers(pattern, real::flags::none, text)};
      EXPECT_EQ(routed, vm_answers(re, text));
      EXPECT_EQ(routed.substr(0, pure.size()), pure);
      ++compared;
    }
  }
  EXPECT(compared == 112);
}

// The walk does the VM's work for a run shape: `(\w+)\s+(\w+)` enumerates without one Pike VM run over a DFA
// window, where every match used to need one (27 % of its instructions on prose). `(\w+)(\d+)` is the control:
// its walk cannot end at the window's end, so the VM still runs, which shows the counter counts.
TEST(dfa_run_shape_needs_no_vm_window)
{
  std::string text;
  while (text.size() < 4096U) {
    text += "the quick fox 42 dogs a9 key = val n=7 aaaaaaaaaaaaaaaaaaaaaaaa 5 ";
  }
  const real::regex walked   {R"((\w+)\s+(\w+))"};
  const real::regex backs_up {R"((\w+)(\d+))"};
  std::size_t       found    {0};
  real::detail::vm_window_runs() = 0;
  for (const auto& m : walked.find_iter(text)) {
    found += m.matched() ? 1U : 0U;
  }
  EXPECT(found > 100U);
  EXPECT_EQ(real::detail::vm_window_runs().load(), 0U);
  // Each route that reaches a window: the walks from candidates (above), the forward pass and reverse
  // (`[a-z]+` walks give way on the long runs), and the inner-literal confirm (`=` is its literal, and `\w`
  // beside `\s` is not one-pass: both start code points with 0xC2). `\s*` is a star loop.
  // A group around one repeated atom, `(\s)+`, is a loop whose body holds saves around the atom.
  for (const std::string_view pattern : {R"(([a-z]+)\s+(\d+))", R"((\w+)\s+=\s+(\w+))", R"((\w+)\s*=\s*(\w+))",
                                         R"((\s)+(\w)+)", R"((\w)+\s+(\w)+)"}) {
    const real::regex re {std::string {pattern}};
    found = 0;
    for (const auto& m : re.find_iter(text)) {
      found += m.matched() ? 1U : 0U;
    }
    EXPECT(found > 50U);
    EXPECT_EQ(real::detail::vm_window_runs().load(), 0U);
  }
  found = 0;
  real::detail::vm_window_runs() = 0;
  for (const auto& m : backs_up.find_iter(text)) {
    found += m.matched() ? 1U : 0U;
  }
  EXPECT(found > 100U);
  EXPECT(real::detail::vm_window_runs().load() > 0U);
}

// count_matches reads no group, so a pattern with groups takes the lazy DFA's span batch like one without:
// counted by the batch, by walking find_iter, and with the DFAs taken out, the three agree.
TEST(dfa_count_of_a_pattern_with_groups_is_batched)
{
  std::string text;
  while (text.size() < 4096U) {
    text += "the quick fox 42 dogs a9 key = val \xC3\xA9t\xC3\xA9 aaaa ";
  }
  for (const std::string_view pattern : {R"((a|e|i|o|u)+)", R"((\w)+)", R"((\w+)\s+(\w+))", R"(([a-z]+)\s+(\d+))",
                                         R"((\s)+(\w)+)", R"((\w+)(\d+))"}) {
    const real::regex re     {std::string {pattern}};
    std::size_t       walked {0};
    for (const auto& m : re.find_iter(text)) {
      walked += m.matched() ? 1U : 0U;
    }
    real::detail::dfa_span_batches() = 0;
    const std::size_t counted {re.count_matches(text)};
    if (pattern != R"((\w+)(\d+))") { // backs up inside its window: not a walk the span filler serves
      EXPECT(real::detail::dfa_span_batches().load() > 0U);
    }
    real::detail::lazy_dfa_route_disabled() = true;
    const std::size_t vm      {re.count_matches(text)};
    real::detail::lazy_dfa_route_disabled() = false;
    EXPECT_EQ(counted, walked);
    EXPECT_EQ(counted, vm);
    EXPECT(counted > 10U);
  }
}

// A line-anchored pattern's candidates are the line starts whose first byte a match can begin with: a line
// that starts with a space is skipped before any seed or DFA walk, which is what makes `(?m)^\w+` over
// prose whose lines start with a space cheap. Pinned on the skip itself, then every query against the VM
// on subjects whose lines start with a space, a word, a digit and nothing.
TEST(line_anchored_candidates_skip_lines_no_match_can_start)
{
  const real::regex                                                      re   {R"(^\w+)", real::flags::multiline};
  const real::detail::program_view                                       prog {re.raw_program()};
  real::detail::dynamic_storage::state_type                              state;
  real::detail::pike_vm<real::detail::dynamic_storage::state_type, true> vm   {prog, state};
  EXPECT(prog.hints.line_anchored);
  EXPECT_EQ(vm.next_candidate("x\n b\n c\ndd", 1, 0), 8U); // the lines " b" and " c" are passed over

  for (const std::string_view pattern : {R"(^\w+)", R"(^[a-z]+$)", R"(^\s*\w+)", R"(^\d+)", R"(^.*$)", R"(^[A-Z]\w*)",
                                         R"(^ \w+)", R"(^\S+ \d+)"}) {
    for (const std::string_view unit : {"the fox\n jumps over\n12 dogs\n\nAlpha beta\n ", "\n \n x\n", "word\n"}) {
      std::string text;
      while (text.size() < 700U) {
        text += unit;
      }
      const real::regex rx     {std::string {pattern}, real::flags::multiline};
      const std::string routed {answers(rx, text)};
      const std::string pure   {pure_answers(pattern, real::flags::multiline, text)};
      EXPECT_EQ(routed, vm_answers(rx, text));
      EXPECT_EQ(routed.substr(0, pure.size()), pure);
    }
  }
}
