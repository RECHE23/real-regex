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

  // Compares find_iter's overall-match span sequence (start/end only, NOT group spans) under
  // route-auto vs forced-general. Restores the toggle unconditionally (even on an EXPECT failure)
  // so one bad case cannot poison the rest of the binary's test run.
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

  // Same as expect_route_toggle_agrees, but also compares group(1) -- the exact surface the
  // possessive-capture-fix touches, which the overall-span-only helper above cannot see.
  void expect_route_toggle_agrees_group1(const real::regex&  re,
                                         std::string_view    text,
                                         std::string_view    ctx)
  {
    struct span { std::size_t start, end, g1s, g1e; };
    const auto collect = [&] {
                           std::vector<span> out;
                           for (const auto& m : re.find_iter(text)) {
                             out.push_back({.start = m.start(), .end = m.end(),
                                            .g1s   = m.start(1), .g1e = m.end(1)});
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
      EXPECT_EQ(auto_route[i].g1s, forced_general[i].g1s);
      EXPECT_EQ(auto_route[i].g1e, forced_general[i].g1e);
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
  EXPECT(prog.hints.possessive_class.kind == real::detail::class_kind::klass);
  EXPECT(prog.hints.possessive_min_nonzero);
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_prefix_size), 0);
}

TEST(route_pin_star_possessive_needs_nonempty_consumption)
{
  // Bare `[a-z]*+` alone (no suffix) is nullable -- must NOT arm (mirrors bare `[a-z]*` never
  // arming greedy_class_loop either; see the guard's own doc comment in prefilter.hpp).
  const real::regex bare  {"[a-z]*+"};
  EXPECT(!bare.raw_program().hints.possessive_class.armed());
  // The same loop with a required suffix always consumes >= 1 byte -- arms.
  const real::regex  suffixed {"[a-z]*+x"};
  const auto         prog     {suffixed.raw_program()};
  EXPECT(prog.hints.possessive_class.kind == real::detail::class_kind::klass);
  EXPECT(!prog.hints.possessive_min_nonzero);
  EXPECT_EQ(prog.hints.possessive_suffix_size, 1U);
}

TEST(route_pin_unicode_word_class_possessive)
{
  const real::regex re   {R"(\w++)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_class.kind == real::detail::class_kind::klass_cp);
}

TEST(route_pin_quoted_delimited_possessive)
{
  const real::regex re   {R"("[^"]*+")"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_class.kind == real::detail::class_kind::klass_cp); // [^"] is a negated class -> klass_cp (wagon-4)
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
  EXPECT(prog.hints.possessive_class.kind == real::detail::class_kind::klass);
  EXPECT(prog.hints.possessive_group_start >= 0);
  EXPECT_EQ(prog.hints.possessive_group_end, static_cast<std::int16_t>(prog.hints.possessive_group_start + 1));
  const real::regex ordinary_group {"([a-z]++)"};
  EXPECT(!ordinary_group.raw_program().hints.possessive_class.armed());
}

TEST(route_pin_kv_prefix_overlaps_body_class_stays_general)
{
  // id=[a-z0-9]*+; -- the prefix's own bytes ('i', 'd') are members of the body class: the
  // eligibility guard (prefilter.hpp) must decline this, or the delimited runner's skip-to-
  // body-end retry could silently miss a leftmost match / degrade to quadratic. See
  // pattern_hints's own doc comment for the full argument.
  const real::regex re   {"id=[a-z0-9]*+;"};
  const auto        prog {re.raw_program()};
  EXPECT(!prog.hints.possessive_class.armed());
}

TEST(route_pin_byte_atom_possessive_captured_now_arms)
{
  // A single literal-byte body wrapped in a CAPTURING group (`(a)*+b`) now arms exactly like
  // klass/cp_class: the possessive-capture-fix taught the shared driver to capture the loop's own
  // LAST iteration (not the whole match), so kind=byte no longer needs to decline captured shapes
  // -- see the capture_ok guard's own doc comment.
  const real::regex re   {"(a)*+b"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_class.kind == real::detail::class_kind::byte);
}

TEST(route_pin_bounded_count_possessive_stays_general)
{
  // A bounded possessive count (`{n,m}+`) has no "any start within the run reaches the identical
  // body end" invariant -- out of scope for the whole train (see pattern_hints's doc comment).
  const real::regex re2   {"[a-z]{2,4}+"};
  const auto        prog2 {re2.raw_program()};
  EXPECT(!prog2.hints.possessive_class.armed());
  const real::regex re1   {"[a-z]{1,4}+"};     // min=1 fits the "one mandatory copy" shape by itself,
  const auto        prog1 {re1.raw_program()}; // but the bounded TAIL has no self-loop jump -- must still decline.
  EXPECT(!prog1.hints.possessive_class.armed());
}

TEST(route_pin_non_wb_trailing_assert_stays_general)
{
  // A non-\b/\B assert_position (here `$`) sitting right after the loop's jump is exactly what
  // peel_optional_trail_wb's "any other assert disqualifies" branch exists for.
  const real::regex re   {"[a-z]++$"};
  const auto        prog {re.raw_program()};
  EXPECT(!prog.hints.possessive_class.armed());
}

TEST(route_pin_unicode_word_class_possessive_with_wb)
{
  // \b\w++\b: the has_wb branch of the CODE-POINT-class arm (route_pin_bare_class_possessive
  // above only exercises it for the byte-class arm via \b[a-z]++\b).
  const real::regex re   {R"(\b\w++\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_class.kind == real::detail::class_kind::klass_cp);
}

TEST(route_pin_mandatory_and_loop_table_mismatch_stays_general)
{
  // [abc] compiles to a byte klass (ASCII-only, `classes` table); a possessive `.*+` loop compiles
  // to a klass_cp loop (Unicode-width "any", `cp_classes` table) -- two DIFFERENT tables that can
  // coincidentally share the same numeric index (both table-index 0). The "same_atom" mandatory-
  // copy check used to compare raw arg16 without verifying the table matched, so this accidental
  // collision let `[abc].*+` silently match ANY input regardless of whether [abc] matched at all
  // -- found live by the 5x100k differential fuzz closure battle (`[abc].*+` matched "x" as (0,1)
  // when the correct answer, confirmed against Python 3.14.6, is no match at all). R2's class_ref
  // makes this comparison a type mismatch rather than a coincidental index collision.
  const real::regex re   {"[abc].*+"};
  const auto        prog {re.raw_program()};
  EXPECT(!prog.hints.possessive_class.armed());
}

TEST(route_pin_byte_possessive_bare_and_suffixed)
{
  // R2 (phase Raffinement): the class_ref{kind=byte} recognizer for byte_loop_possessive, closing
  // the asymmetry `a++`/`a*+x` had with the class/cp_class family (emitted and executed by the
  // general VM, but with no dedicated recognizer/runner until now).
  const real::regex re   {"a++"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.possessive_class.kind == real::detail::class_kind::byte);
  EXPECT_EQ(static_cast<int>(prog.hints.possessive_class.index), static_cast<int>('a'));
  EXPECT(prog.hints.possessive_min_nonzero);
  const real::regex star_suffixed {"x*+y"};
  const auto        prog2         {star_suffixed.raw_program()};
  EXPECT(prog2.hints.possessive_class.kind == real::detail::class_kind::byte);
  EXPECT(!prog2.hints.possessive_min_nonzero);
  EXPECT_EQ(prog2.hints.possessive_suffix_size, 1U);
}

TEST(route_pin_byte_possessive_captured_last_iteration)
{
  // Possessive-capture-fix: (a)*+b on "aaab" now arms AND captures correctly on the fast path --
  // group(1) is the LAST successful iteration ("a" at [2,3)), not the whole loop span, matching
  // re's own semantics (oracle-verified against Python 3.14.6) and the general VM's own answer.
  const real::regex re {"(a)*+b"};
  EXPECT(re.raw_program().hints.possessive_class.armed());
  const auto m = re.search("aaab");
  EXPECT(m.matched());
  if (m.matched()) {
    EXPECT_EQ(m.start(1), 2U);
    EXPECT_EQ(m.end(1), 3U);
  }
}

TEST(route_pin_byte_possessive_wb_wrapped_stays_general)
{
  // R2's byte recognizer declines the wb-wrapped shape outright (documented, not silently
  // dropped): a literal byte has no "word class" to resolve B-1 eligibility against.
  const real::regex re {R"(\ba++\b)"};
  EXPECT(!re.raw_program().hints.possessive_class.armed());
}

TEST(mandatory_and_loop_table_mismatch_oracle_pinned)
{
  // Oracle-verified against Python 3.14.6: re.search(r"[abc].*+", text).
  const real::regex re {"[abc].*+"};
  EXPECT(!re.search("x").matched());
  EXPECT(!re.search("xyz").matched());
  const auto m1 = re.search("bxyz");
  EXPECT(m1.matched());
  if (m1.matched()) {
    EXPECT_EQ(m1.start(), 0U);
    EXPECT_EQ(m1.end(), 4U);
  }
  const auto m2 = re.search("xbxyz");
  EXPECT(m2.matched());
  if (m2.matched()) {
    EXPECT_EQ(m2.start(), 1U);
    EXPECT_EQ(m2.end(), 5U);
  }
}

TEST(possessive_fastpath_route_toggle_mandatory_loop_table_mismatch)
{
  // wagon-4: even though this shape now correctly declines the fast path (route-pin test above),
  // pin the route-toggle differential too so a regression that makes it arm again is caught by
  // BOTH signals (route-pin AND behavioral agreement), not hint inspection alone.
  const real::regex re {"[abc].*+"};
  expect_route_toggle_agrees(re, "xbxyz"sv, "mandatory/loop table mismatch");
}

TEST(mandatory_and_loop_table_mismatch_exact_original_repro)
{
  // The exact minimized repro from the fuzz battle (a general atomic-group shape, NOT even a
  // direct possessive quantifier in the source pattern -- `(?>C)` and `(?>[\w·]*)` compile down to
  // the identical mandatory-copy + possessive-loop bytecode shape as `C[\w·]*+`), pinned verbatim.
  // Oracle-verified against Python 3.14.6.
  const real::regex re {R"((?>C)(?>[\w·]*))", real::flags::icase};
  EXPECT(!re.search("x").matched());
  const auto m = re.search("abc");
  EXPECT(m.matched());
  if (m.matched()) {
    EXPECT_EQ(m.start(), 2U);
    EXPECT_EQ(m.end(), 3U);
  }
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

// --- possessive-capture-fix: oracle-verified matrix ({byte,klass,klass_cp} x {*+,++} x -----------
// {dense,sparse,zero-iteration,multi-byte-last-atom}) -- run_possessive_loop_generic's LastWidth
// mechanism must capture the loop's own LAST iteration, not the whole match. Every span below is
// oracle-verified against Python 3.14.6 (re.search, byte offsets recomputed from the codepoint
// spans it reports). Reversion-proven: fails against pre-fix f28a604 (whole-match capture), passes
// here (git worktree diff, see the commit message).

TEST(capture_fix_byte_dense_and_sparse)
{
  // Captured bare `++` (no suffix) never arms the fast path (only the star+mandatory-suffix
  // "embedded capture Tier 1" shape does -- see route_pin_captured_class_possessive's own doc
  // comment), so the armed shape needs a suffix: `(a)*+;`.
  // dense: the run starts at text[0], no leading noise.
  const real::regex re {"(a)*+;"};
  EXPECT(re.raw_program().hints.possessive_class.armed());
  const auto m1        {re.search("aaa;")};
  EXPECT(m1.matched());
  EXPECT_EQ(m1.start(), 0U);
  EXPECT_EQ(m1.end(), 4U);
  EXPECT_EQ(m1.start(1), 2U);
  EXPECT_EQ(m1.end(1), 3U);
  // sparse: leading noise (no 'a' or ';') the loop must skip over via seed_viable before landing
  // on the run.
  const auto m2 {re.search("xxyyaaa;")};
  EXPECT(m2.matched());
  EXPECT_EQ(m2.start(), 4U);
  EXPECT_EQ(m2.end(), 8U);
  EXPECT_EQ(m2.start(1), 6U);
  EXPECT_EQ(m2.end(1), 7U);
}

TEST(capture_fix_byte_zero_iteration)
{
  // (a)*+b on "b": the loop runs zero times -- group 1 must stay real::npos (Python's None), NOT
  // collapse to the empty span [0,0) the old whole-match-span bug would have produced.
  const real::regex re  {"(a)*+b"};
  const auto        m0  {re.search("b")};
  EXPECT(m0.matched());
  EXPECT_EQ(m0.start(), 0U);
  EXPECT_EQ(m0.end(), 1U);
  EXPECT_EQ(m0.start(1), real::npos);
  EXPECT_EQ(m0.end(1), real::npos);
  // same shape with >0 iterations, for contrast within the same TEST.
  const auto m1 {re.search("aaab")};
  EXPECT(m1.matched());
  EXPECT_EQ(m1.start(1), 2U);
  EXPECT_EQ(m1.end(1), 3U);
}

TEST(capture_fix_klass_dense_and_sparse)
{
  const real::regex re {"([a-z])*+;"};
  EXPECT(re.raw_program().hints.possessive_class.armed());
  const auto m1        {re.search("abcxyz;")};
  EXPECT(m1.matched());
  EXPECT_EQ(m1.start(), 0U);
  EXPECT_EQ(m1.end(), 7U);
  EXPECT_EQ(m1.start(1), 5U);
  EXPECT_EQ(m1.end(1), 6U);
  const auto m2 {re.search("9900--abcxyz;")};
  EXPECT(m2.matched());
  EXPECT_EQ(m2.start(), 6U);
  EXPECT_EQ(m2.end(), 13U);
  EXPECT_EQ(m2.start(1), 11U);
  EXPECT_EQ(m2.end(1), 12U);
}

TEST(capture_fix_klass_zero_iteration)
{
  const real::regex re {"([a-z])*+;"};
  const auto        m0 {re.search(";")};
  EXPECT(m0.matched());
  EXPECT_EQ(m0.start(1), real::npos);
  EXPECT_EQ(m0.end(1), real::npos);
  const auto m1 {re.search("abc;")};
  EXPECT(m1.matched());
  EXPECT_EQ(m1.start(1), 2U);
  EXPECT_EQ(m1.end(1), 3U);
}

TEST(capture_fix_klass_cp_dense_and_sparse)
{
  // \w is Unicode-width (klass_cp) -- "café" ends on the 2-byte codepoint 'é' (0xC3 0xA9), the
  // exact shape codepoint_retreat exists to walk back over.
  const real::regex re {R"((\w)*+;)"};
  EXPECT(re.raw_program().hints.possessive_class.armed());
  const auto m1        {re.search("caf\xC3\xA9;"sv)};
  EXPECT(m1.matched());
  EXPECT_EQ(m1.start(), 0U);
  EXPECT_EQ(m1.end(), 6U);
  EXPECT_EQ(m1.start(1), 3U);
  EXPECT_EQ(m1.end(1), 5U);
  const auto m2 {re.search("   caf\xC3\xA9;"sv)};
  EXPECT(m2.matched());
  EXPECT_EQ(m2.start(), 3U);
  EXPECT_EQ(m2.end(), 9U);
  EXPECT_EQ(m2.start(1), 6U);
  EXPECT_EQ(m2.end(1), 8U);
}

TEST(capture_fix_klass_cp_zero_iteration)
{
  const real::regex re {R"((\w)*+;)"};
  const auto        m0 {re.search(";")};
  EXPECT(m0.matched());
  EXPECT_EQ(m0.start(1), real::npos);
  EXPECT_EQ(m0.end(1), real::npos);
}

TEST(capture_fix_klass_cp_ascii_last_atom)
{
  const real::regex re {R"((\w)*+ )"};
  const auto        m  {re.search("hello ")};
  EXPECT(m.matched());
  EXPECT_EQ(m.start(), 0U);
  EXPECT_EQ(m.end(), 6U);
  EXPECT_EQ(m.start(1), 4U);
  EXPECT_EQ(m.end(1), 5U);
}

TEST(capture_fix_klass_cp_multibyte_last_atom_4byte)
{
  // U+1F600 (grinning face) is the maximal 4-byte UTF-8 case -- the exact boundary
  // codepoint_retreat's own 4-byte cap exists for. Body excludes ';' (a negated class, klass_cp)
  // so the possessive loop cannot over-consume the delimiter.
  const real::regex re   {R"(([^;])*+;)"};
  const std::string text {"ab\xF0\x9F\x98\x80;"}; // "ab" + U+1F600 (F0 9F 98 80) + ';'
  const auto        m    {re.search(text)};
  EXPECT(m.matched());
  EXPECT_EQ(m.start(), 0U);
  EXPECT_EQ(m.end(), 7U);
  EXPECT_EQ(m.start(1), 2U);
  EXPECT_EQ(m.end(1), 6U); // 4-byte codepoint: [2,6)
  EXPECT_EQ(text.substr(m.start(1), m.end(1) - m.start(1)), "\xF0\x9F\x98\x80");
}

TEST(capture_fix_bounded_count_general_vm_parity)
{
  // {n,m}+ never arms the fast path (route_pin_bounded_count_possessive_stays_general) -- always
  // runs on the general VM, which already captured correctly (this bug was fast-path-only). Pinned
  // here as the matrix's control cell: parity with re, unaffected by this fix, must stay flat.
  const real::regex re {"(a){2,4}+b"};
  const auto        m1 {re.search("aaaab")};
  EXPECT(m1.matched());
  EXPECT_EQ(m1.start(1), 3U);
  EXPECT_EQ(m1.end(1), 4U);
  const auto m2 {re.search("aab")};
  EXPECT(m2.matched());
  EXPECT_EQ(m2.start(1), 1U);
  EXPECT_EQ(m2.end(1), 2U);
}

TEST(capture_fix_route_toggle_all_kinds)
{
  // wagon-4: route-auto (fast path) vs forced-general must agree on every captured-possessive
  // shape across all three loop kinds, including the zero-iteration and multi-byte-last-atom
  // corners above -- group(1), not just the overall span (expect_route_toggle_agrees_group1 is
  // the only helper in this file that can see the capture-fix's own surface).
  expect_route_toggle_agrees_group1(real::regex {"(a)*+;"}, "xxyyaaa; ;yaaaa;"sv, "capture-fix byte");
  expect_route_toggle_agrees_group1(real::regex {"(a)*+b"}, "b aaab xaaaab"sv, "capture-fix byte zero-iter");
  expect_route_toggle_agrees_group1(real::regex {"([a-z])*+;"}, "9900--abcxyz; ;xyz;"sv, "capture-fix klass");
  expect_route_toggle_agrees_group1(real::regex {R"((\w)*+;)"}, "   caf\xC3\xA9; ;h\xC3\xA9llo;"sv, "capture-fix klass_cp");
  expect_route_toggle_agrees_group1(real::regex {R"(([^;])*+;)"}, "ab\xF0\x9F\x98\x80; ;xyz;"sv, "capture-fix klass_cp 4-byte");
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
