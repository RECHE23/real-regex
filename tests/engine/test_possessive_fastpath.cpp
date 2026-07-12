// D1-perf (Étage A): possessive class+/++ loop fast paths -- route-pinning (a regression of
// recognition is a silent perf loss, mirrors test_route_pinning.cpp) plus the MANDATORY
// route-toggle differential (route-auto vs forced-general must agree on every input -- the
// "wagon-4 pattern" applied to these new recognizers/runners). Gate-safe: no wall-clock.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace {

  // Compares find_iter's full span+group sequence under route-auto vs forced-general. Restores
  // the toggle unconditionally (even on an EXPECT failure) so one bad case cannot poison the rest
  // of the binary's test run.
  void expect_route_toggle_agrees(const real::regex&  re,
                                  std::string_view    text,
                                  std::string_view    ctx)
  {
    struct span { std::size_t start, end; };
    const auto collect = [&] {
                           std::vector<span> out;
                           for (const auto& m : re.find_iter(text)) {
                             out.push_back({.start = m.start(), .end = m.end()});
                           }
                           return out;
                         };
    real::detail::possessive_fastpath_disabled() = false;
    const auto auto_route     {collect()};
    real::detail::possessive_fastpath_disabled() = true;
    const auto forced_general {collect()};
    real::detail::possessive_fastpath_disabled() = false;
    EXPECT_EQ(auto_route.size(), forced_general.size());
    const std::size_t n {std::min(auto_route.size(), forced_general.size())};
    for (std::size_t i {0}; i < n; ++i) {
      EXPECT_EQ(auto_route[i].start, forced_general[i].start);
      EXPECT_EQ(auto_route[i].end, forced_general[i].end);
    }
    (void) ctx;
  }

  std::string random_text(std::mt19937& rng,
                          std::size_t   len)
  {
    static constexpr std::string_view          alphabet {"abcxyzABC012\"'; =\n\t"};
    std::uniform_int_distribution<std::size_t> d(0, alphabet.size() - 1);
    std::string                                s;
    s.reserve(len);
    for (std::size_t i {0}; i < len; ++i) {
      s += alphabet[d(rng)];
    }
    return s;
  }
} // namespace

// --- route pinning: the new hints arm on the exact shapes they are meant for ------------------

TEST(route_pin_bare_class_possessive)
{
  const real::regex re   {"[a-z]++"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_class_loop >= 0);
  EXPECT(prog.hints.possessive_min_nonzero);
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_prefix_size), 0);
}

TEST(route_pin_star_possessive_needs_nonempty_consumption)
{
  // Bare `[a-z]*+` alone (no suffix) is nullable -- must NOT arm (mirrors bare `[a-z]*` never
  // arming greedy_class_loop either; see the guard's own doc comment in prefilter.hpp).
  const real::regex bare  {"[a-z]*+"};
  EXPECT_EQ(static_cast<int>(bare.raw_program().hints.possessive_class_loop), -1);
  // The same loop with a required suffix always consumes >= 1 byte -- arms.
  const real::regex  suffixed {"[a-z]*+x"};
  const auto         prog     {suffixed.raw_program()};
  EXPECT(prog.hints.possessive_class_loop >= 0);
  EXPECT(!prog.hints.possessive_min_nonzero);
  EXPECT_EQ(prog.hints.possessive_suffix_size, 1U);
}

TEST(route_pin_unicode_word_class_possessive)
{
  const real::regex re   {R"(\w++)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_class_loop), -1);
}

TEST(route_pin_quoted_delimited_possessive)
{
  const real::regex re   {R"("[^"]*+")"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_cp_class >= 0); // [^"] is a negated class -> klass_cp (wagon-4)
  EXPECT_EQ(prog.hints.possessive_prefix_size, 1U);
  EXPECT_EQ(prog.hints.possessive_suffix_size, 1U);
}

TEST(route_pin_captured_class_possessive)
{
  // The quantifier must be on the GROUP itself (`([a-z])*+b`) for Tier 1's embedded-capture
  // mechanism (primary_target IS the capture slot, no separate save pair) to apply -- `([a-z]++)`
  // is a different AST shape (an ordinary capturing group wrapping an ALREADY-possessive class,
  // compiled with plain save/save instructions around it) and correctly stays out of this fast
  // path's scope (caught empirically: an earlier, wrong hand-derived version of this test
  // expected `([a-z]++)` to arm and it does not).
  const real::regex re   {"([a-z])*+b"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_class_loop >= 0);
  EXPECT(prog.hints.possessive_group_start >= 0);
  EXPECT_EQ(prog.hints.possessive_group_end, static_cast<std::int16_t>(prog.hints.possessive_group_start + 1));
  const real::regex ordinary_group {"([a-z]++)"};
  EXPECT_EQ(static_cast<int>(ordinary_group.raw_program().hints.possessive_class_loop), -1);
}

TEST(route_pin_kv_prefix_overlaps_body_class_stays_general)
{
  // id=[a-z0-9]*+; -- the prefix's own bytes ('i', 'd') are members of the body class: the
  // eligibility guard (prefilter.hpp) must decline this, or the delimited runner's skip-to-
  // body-end retry could silently miss a leftmost match / degrade to quadratic. See
  // pattern_hints's own doc comment for the full argument.
  const real::regex re   {"id=[a-z0-9]*+;"};
  const auto        prog {re.raw_program()};
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_class_loop), -1);
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_cp_class), -1);
}

TEST(route_pin_byte_atom_possessive_stays_general)
{
  // A single literal-byte body (`a*+`, `(a)*+b`) is out of this train's scope -- greedy has no
  // dedicated whole-pattern byte-loop fast path either (`a+` alone stays general/lazy-DFA too).
  const real::regex re   {"(a)*+b"};
  const auto        prog {re.raw_program()};
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_class_loop), -1);
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_cp_class), -1);
}

TEST(route_pin_bounded_count_possessive_stays_general)
{
  // A bounded possessive count (`{n,m}+`) has no "any start within the run reaches the identical
  // body end" invariant -- out of scope for the whole train (see pattern_hints's doc comment).
  const real::regex re2   {"[a-z]{2,4}+"};
  const auto        prog2 {re2.raw_program()};
  EXPECT_EQ(static_cast<int>(prog2.hints.possessive_class_loop), -1);
  const real::regex re1   {"[a-z]{1,4}+"};     // min=1 fits the "one mandatory copy" shape by itself,
  const auto        prog1 {re1.raw_program()}; // but the bounded TAIL has no self-loop jump -- must still decline.
  EXPECT_EQ(static_cast<int>(prog1.hints.possessive_class_loop), -1);
}

TEST(route_pin_non_wb_trailing_assert_stays_general)
{
  // A non-\b/\B assert_position (here `$`) sitting right after the loop's jump is exactly what
  // peel_optional_trail_wb's "any other assert disqualifies" branch exists for.
  const real::regex re   {"[a-z]++$"};
  const auto        prog {re.raw_program()};
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_class_loop), -1);
}

TEST(route_pin_unicode_word_class_possessive_with_wb)
{
  // \b\w++\b: the has_wb branch of the CODE-POINT-class arm (route_pin_bare_class_possessive
  // above only exercises it for the byte-class arm via \b[a-z]++\b).
  const real::regex re   {R"(\b\w++\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_cp_class >= 0);
}

// --- route-toggle differential: route-auto vs forced-general must always agree -----------------

TEST(possessive_fastpath_route_toggle_quoted)
{
  const real::regex re   {R"("[^"]*+")"};
  const std::string text {R"(a="1" b="two words" c="" d="unterminated)"};
  expect_route_toggle_agrees(re, text, "quoted");
}

TEST(possessive_fastpath_route_toggle_bare_and_suffixed)
{
  const real::regex bare  {"[a-z]++"};
  const real::regex suff  {R"(\d++x)"};
  const std::string text1 {"abc 123 def456 ghi"};
  const std::string text2 {"12x 4x abc 99999x 0x"};
  expect_route_toggle_agrees(bare, text1, "bare");
  expect_route_toggle_agrees(suff, text2, "suffixed");
}

TEST(possessive_fastpath_route_toggle_wb_and_captured)
{
  const real::regex wb   {R"(\b[a-z]++\b)"};
  const real::regex cap  {"([a-z])*+b"}; // the embedded-capture shape (route_pin_captured_class_possessive)
  const std::string text {"A9z hello world 42abc xyzb aaab"};
  expect_route_toggle_agrees(wb, text, "wb");
  expect_route_toggle_agrees(cap, text, "captured");
}

TEST(possessive_fastpath_route_toggle_unicode_word_class)
{
  const real::regex re   {R"(\w++)"};
  const std::string text {"héllo wörld café \xC3\xA9 tests 123 _under_score"};
  expect_route_toggle_agrees(re, text, "unicode");
}

TEST(possessive_fastpath_route_toggle_kv_declined_shape_still_correct)
{
  // Declined by the eligibility guard (stays general on BOTH sides of the toggle) -- the
  // differential must still trivially agree (nothing to disagree about), proving the decline
  // itself does not change behavior.
  const real::regex re   {"id=[a-z0-9]*+;"};
  const std::string text {"id=abc; id=id=nested; x=y; id=;"};
  expect_route_toggle_agrees(re, text, "kv-declined");
}

TEST(possessive_fastpath_route_toggle_adversarial_no_closing_delimiter)
{
  // No closing quote anywhere -- the delimited runner's retry must terminate cleanly (not hang),
  // and both sides of the toggle must agree there is no match.
  const std::string text (500, 'a');
  const real::regex re {R"("[^"]*+")"};
  expect_route_toggle_agrees(re, text, "no-closing-delim");
  EXPECT(!re.search(text));
}

TEST(possessive_fastpath_route_toggle_fullmatch_and_prefix_modes)
{
  const real::regex re           {R"("[^"]*+")"};
  const std::string quoted       {R"("hello")"};
  const std::string unterminated {R"("hello)"};
  real::detail::possessive_fastpath_disabled() = false;
  const auto full_a              {re.fullmatch(quoted)};
  const auto pre_a               {re.match(quoted)};
  const auto full_a2             {re.fullmatch(unterminated)};
  real::detail::possessive_fastpath_disabled() = true;
  const auto full_g              {re.fullmatch(quoted)};
  const auto pre_g               {re.match(quoted)};
  const auto full_g2             {re.fullmatch(unterminated)};
  real::detail::possessive_fastpath_disabled() = false;
  EXPECT_EQ(full_a.matched(), full_g.matched());
  EXPECT_EQ(full_a.start(), full_g.start());
  EXPECT_EQ(full_a.end(), full_g.end());
  EXPECT_EQ(pre_a.matched(), pre_g.matched());
  EXPECT_EQ(full_a2.matched(), full_g2.matched());
  EXPECT(!full_a2.matched());
}

TEST(possessive_fastpath_full_prefix_mode_failure_branches)
{
  // The full/prefix (anchored, single-attempt, no retry) branches of run_possessive_loop_generic
  // -- each of its distinct failure points -- both for the delimited shape (too little text for
  // the prefix, a prefix mismatch, trailing bytes after a full match) and the bare/suffixed shape
  // (min>=1 with a non-class start, trailing bytes after a full match).
  const real::regex delim {R"("[^"]*+")"};
  EXPECT(!delim.match(""sv));                  // available < prefix_size
  EXPECT(!delim.match(R"(xhello")"sv));        // prefix mismatch at the anchor
  EXPECT(!delim.fullmatch(R"("hi"extra)"sv));  // full mode: bytes remain after the closing delimiter

  const real::regex suffixed {R"(\d++x)"};
  EXPECT(!suffixed.match("abcx"sv));           // min_nonzero, start not in class
  EXPECT(!suffixed.fullmatch("123xyz"sv));     // full mode: bytes remain after the suffix
  EXPECT(suffixed.fullmatch("123x"sv).matched());

  expect_route_toggle_agrees(delim, R"(xhello" "hi"extra "a")"sv, "delim-full-prefix-fail");
  expect_route_toggle_agrees(suffixed, "abcx 123xyz 123x"sv, "suffixed-full-prefix-fail");
}

TEST(possessive_fastpath_route_toggle_randomized_sweep)
{
  // A broader randomized sweep across body/quantifier/prefix/suffix combinations (the same
  // corpus shape D1-perf Étage A's own verification used) -- kept small enough for the gate
  // (a few hundred checks), not a substitute for the offline fuzz run, a permanent regression
  // pin for it.
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed)
  std::mt19937                               rng      {20260712};
  const std::vector<std::string>             bodies   {"[a-z]", "[a-z0-9]", R"(\d)", R"(\w)", R"([^"])", "[^;]"};
  const std::vector<std::string>             quants   {"*+", "++"};
  const std::vector<std::string>             prefixes {"", "\"", ";", "id="};
  const std::vector<std::string>             suffixes {"", "\"", ";", "x"};
  std::uniform_int_distribution<std::size_t> bd(0, bodies.size() - 1);
  std::uniform_int_distribution<std::size_t> qd(0, quants.size() - 1);
  std::uniform_int_distribution<std::size_t> pd(0, prefixes.size() - 1);
  std::uniform_int_distribution<std::size_t> sd(0, suffixes.size() - 1);
  for (int i {0}; i < 40; ++i) {
    const std::string          pattern {prefixes[pd(rng)] + bodies[bd(rng)] + quants[qd(rng)] + suffixes[sd(rng)]};
    std::optional<real::regex> re;
    try {
      re.emplace(pattern);
    }
    catch (const real::regex_error&) {
      continue;
    }
    const std::string text {random_text(rng, 40)};
    expect_route_toggle_agrees(*re, text, pattern);
  }
}
