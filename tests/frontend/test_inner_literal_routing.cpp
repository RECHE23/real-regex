//! IL.2: the routed==core differential. The inner-literal search path must give byte-identical results to the
//! core Pike search — every match span, on a corpus plus adversarial cases (multi-occurrence, orphan hits, the
//! overlap where a confirm-match need not contain the current literal hit). The route toggle proves it within
//! one binary; the D3 acid pins linearity.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp> // inner_literal_route_disabled
#include <real/real.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
  std::vector<std::pair<std::size_t, std::size_t>> spans(std::string_view pat,
                                                         std::string_view text)
  {
    const real::regex                                re {pat};
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (const auto& m : re.find_iter(text)) {
      out.emplace_back(m.start(0), m.end(0));
    }
    return out;
  }
}

TEST(inner_literal_routed_equals_core)
{
  struct testcase { std::string_view pat; std::string_view text; };
  const testcase cases[] {
    {.pat = R"(\d{4}-\d{2}-\d{2})", .text = "log 2026-07-04 x 2026-12-25 e no-date 99-99-99 here"},
    {.pat = R"((\w+)@(\w+))",       .text = "a@b x@y c@d word noat foo@bar @ trailing@"},
    {.pat = R"(key=(\w+))",         .text = "key=val key=x notkey= key=z end key="},
    {.pat = R"(\w+@)",              .text = "ab@ cd@ @ orphan@ ef@ trailing no"},
    {.pat = R"(@\w+)",              .text = "@a @bc @ @def head @"},
    {.pat = R"(a\d+X)",             .text = "aX a1X aa12X 12X a123Xzz X aXX"},                      // literal X, prefix a\d+ (overlap)
    {.pat = R"(\d{4}-\d{2})",       .text = "9999-99 12-3456-78 no 0000-00"},
    {.pat = R"(GET /\w+ )",         .text = "GET /a POST /b GET /home x GET / y GET /ok "},
    {.pat = R"(((.))a)",             .text = "aaaab aabaa baaaa aXaya"},                            // the exhaustive-flagged weak/adjacent literal
    {.pat = R"((a?)a)",              .text = "aaa baa aXa aaaa"},                                   // adjacent literal, optional prefix
    {.pat = R"(.+@)",                .text = "aaaa@x bb@ c@d@e no @"},                              // greedy prefix: reverse must not over-bound
    {.pat = R"(id=[0-9a-f]{8})",     .text = "id=abc12345 id=00000000 x id=deadbeef no id= id=zz"}, // offset-0 literal
    // IL-FUSION cases: the whole pattern is fixed_shape, so these take the
    // arithmetic-verify fast path (match_byte_klass_run, no reverse/forward DFA) instead of the
    // reverse-DFA + confirm_at route the cases above still exercise. `\d` is deliberately NOT used here:
    // the Unicode shorthand compiles to klass_cp, which fixed_shape (and so il_fused_eligible) always
    // excludes -- explicit byte classes are what the fused path (and the benchmarked "date" case,
    // bench_engines.cpp) actually compiles to.
    {.pat = R"([0-9]{4}-[0-9]{2}-[0-9]{2})", .text = "x2026-07-04y 2026-13-99 no-date 99-99-99 z2026-12-25w"},  // hits abutting non-digit/dash noise
    {.pat = R"(([0-9]{4})-([0-9]{2})-([0-9]{2}))", .text = "log 2026-07-04 x 2026-99-99 bad-date 2099-01-01!"}, // grouped: exercises fill_fixed_saves on the fused path
    {.pat = R"([0-9]{4}-[0-9]{2}-[0-9]{2})", .text = "2026-07-04"},                                             // literal hit at h == prefix width exactly (h - prefix_w == 0, the tightest bounds-guard case)
    {.pat = R"([0-9]{4}-[0-9]{2}-[0-9]{2})", .text = "9-04"},                                                   // prefix cannot fit before the hit at all (h < prefix_w) -- must decline, not underflow
    {.pat = R"([0-9]{10}-[0-9]{10}-[0-9]{10})", .text = "x0123456789-0123456789-01234567890y bad"},             // total width 32 (== il_fused_max_width, the boundary): still fused
    {.pat = R"([0-9]{15}-[0-9]{15}-[0-9]{15})", .text = "012345678901234-012345678901234-012345678901234"},     // total width 47 (> il_fused_max_width): stays on the pre-fusion route, must still match correctly
    // Pure-lit alt: StatusLine arms IL `req=`; URL (`s?`) deliberately declines IL (measured).
    // Both stay 0-div route-on vs core.
    {.pat  = R"(https?://[^\s]+)", .text = "see http://a.com and https://b.org/x end no-url http:/bad"},
    {.pat  = R"((info|error|warn)\s+\d{4}-\d{2}-\d{2}\s+req=[a-f0-9]+)",
     .text = "x info 2024-01-15 req=deadbeef y error 2023-12-01 req=abc warn 2020-01-01 req=0 z info_x req=no"},
    {.pat  = R"((abc|xreq=y)z)", .text = "xreq=yz abcz xreq= noz abczz"}, // unsound trap: only common `z` is IL
  };
  // These inputs are tiny (< the small-haystack guard's floor), so the guard would send the routed run back to
  // the core and the comparison would be trivially true. Disable it: the point is to exercise the route.
  real::detail::inner_literal_guard_disabled() = true;
  for (const testcase& tc : cases) {
    real::detail::inner_literal_route_disabled() = true;
    const auto core   {spans(tc.pat, tc.text)};
    real::detail::inner_literal_route_disabled() = false;
    const auto routed {spans(tc.pat, tc.text)};
    EXPECT(core == routed);
  }
  real::detail::inner_literal_route_disabled() = false;
  real::detail::inner_literal_guard_disabled() = false;
}

TEST(inner_literal_fusion_group_captures_match_core)
{
  // The span-only differential above does not read sub-groups; the fused path fills them via
  // fill_fixed_saves (constant offsets from the match start, no re-match) instead of one-pass
  // extraction, so pin the GROUP VALUES themselves, routed vs core, on a fixed-shape pattern with an
  // inner literal and multiple captures. Explicit byte classes, not \d (klass_cp -- not fixed_shape).
  const real::regex re   {R"(([0-9]{4})-([0-9]{2})-([0-9]{2}))"};
  const std::string text {"log 2026-07-04 x bad-date 2099-12-25 end"};

  real::detail::inner_literal_guard_disabled() = true;
  real::detail::inner_literal_route_disabled() = true;
  const auto core   {re.find_all(text)};
  real::detail::inner_literal_route_disabled() = false;
  const auto routed {re.find_all(text)};
  real::detail::inner_literal_route_disabled() = false;
  real::detail::inner_literal_guard_disabled() = false;

  EXPECT_EQ(core.size(), 2U);
  EXPECT_EQ(routed.size(), core.size());
  for (std::size_t i = 0; i < core.size(); ++i) {
    EXPECT_EQ(routed[i][0], core[i][0]); // whole match
    EXPECT_EQ(routed[i][1], core[i][1]); // year
    EXPECT_EQ(routed[i][2], core[i][2]); // month
    EXPECT_EQ(routed[i][3], core[i][3]); // day
  }
  EXPECT_EQ(core[0][1], std::string_view("2026"));
  EXPECT_EQ(core[0][2], std::string_view("07"));
  EXPECT_EQ(core[0][3], std::string_view("04"));
}

TEST(inner_literal_d3_acid_stays_linear)
{
  // D3: the literal every few bytes, every confirm failing. The reverse bound + the sticky abandon must keep
  // it linear — a quadratic loop over ~100 KB would take many seconds (minutes under sanitizers); linear is ms.
  std::string text;
  for (int i = 0; i < 50000; ++i) {
    text += "x-"; // "-" every two bytes, never preceded by four digits -> every confirm fails
  }
  const real::regex re {R"(\d{4}-\d{2})"};
  const auto        t0 {std::chrono::steady_clock::now()};
  std::size_t       n  {0};
  for (const auto& m : re.find_iter(text)) {
    (void) m;
    ++n;
  }
  const auto ms {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()};
  EXPECT(n == 0);
  EXPECT(ms < 5000); // generous even under sanitizers; a quadratic scan would not finish in time
  real::detail::inner_literal_route_disabled() = true;
}

TEST(inner_literal_fusion_d3_acid_stays_linear)
{
  // The same D3 acid, but on a fixed_shape pattern (explicit byte classes, not \d) so it actually
  // exercises the FUSED verify's own linearity, not just the pre-fusion reverse-DFA route's: the fused
  // path deliberately does not advance min_pre_start on a failed candidate (match_byte_klass_run
  // reports pass/fail only), so this pins that the omission does not reopen the quadratic risk the
  // guard exists for -- linear because each candidate is a hard-bounded O(il_fused_max_width) check,
  // not because the guard caught it.
  real::detail::inner_literal_route_disabled() = false; // undo the previous test's trailing state
  std::string text;
  for (int i = 0; i < 50000; ++i) {
    text += "x-"; // "-" every two bytes, never preceded by four digits -> every fused verify fails
  }
  const real::regex re {R"([0-9]{4}-[0-9]{2})"};
  const auto        t0 {std::chrono::steady_clock::now()};
  std::size_t       n  {0};
  for (const auto& m : re.find_iter(text)) {
    (void) m;
    ++n;
  }
  const auto ms {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()};
  EXPECT(n == 0);
  EXPECT(ms < 5000); // generous even under sanitizers; a quadratic scan would not finish in time
  real::detail::inner_literal_route_disabled() = true;
}

//! IL.6: reverse-by-class. When the inner-literal PREFIX is exactly one greedy class loop the match start is
//! the beginning of the class run ending at the candidate — a backward walk, so no prefix sub-program, no
//! reverse DFA and no per-regex cache. That is what lets `static_regex`, which has none of those, take this
//! route; and the same walk replaces the reverse DFA on the dynamic path.
TEST(inner_literal_reverse_by_class_fires_on_exactly_the_class_loop_prefixes)
{
  const auto rev_class {[](const char* pattern) {
                          const real::detail::ast tree {real::detail::parse(pattern, real::flags::none)};
                          return real::detail::compile(tree, real::flags::none | tree.inline_flags).hints.il_rev_class;
                        }};
  const auto rev_is_cp {[](const char* pattern) {
                          const real::detail::ast tree {real::detail::parse(pattern, real::flags::none)};
                          return real::detail::compile(tree, real::flags::none | tree.inline_flags).hints.il_rev_is_cp;
                        }};

  EXPECT(rev_class("[a-z]+@[a-z]+") >= 0);  // byte class
  EXPECT(!rev_is_cp("[a-z]+@[a-z]+"));
  EXPECT(rev_class(R"(\w+-\w+)") >= 0);     // code-point class
  EXPECT(rev_is_cp(R"(\w+-\w+)"));
  EXPECT(rev_class(R"(\d+\.\d+)") >= 0);
  EXPECT(rev_class(R"((\w+)@(\w+))") >= 0); // capture groups around the loop are transparent here

  // A fixed repeat count emits the atom N times with NO split, so it is not a loop and must not be taken
  // for one -- a backward walk would run past the four digits it is allowed.
  EXPECT_EQ(rev_class(R"(\d{4}-\d{2}-\d{2})"), -1);
  EXPECT_EQ(rev_class("[0-9]{4}-[0-9]{2}-[0-9]{2}"), -1);
  EXPECT_EQ(rev_class("a+b+c"), -1);      // two children before the literal, not one
  EXPECT_EQ(rev_class("(?:ab)+@x"), -1);  // the loop body is a sequence, not a class
  EXPECT_EQ(rev_class("[a-z]+"), -1);     // no inner literal at all
  EXPECT_EQ(rev_class("dog"), -1);        // literal at the head
}

TEST(inner_literal_reverse_by_class_matches_the_core)
{
  // Same differential as IL.2, aimed at the shapes the backward walk now serves, and at the edges where an
  // off-by-one in it would show: a literal at position 0 (no prefix room), runs that reach the subject
  // start, adjacent and repeated literals, multi-byte members either side of the boundary, and a truncated
  // UTF-8 tail (the walk must not step into it).
  const char* patterns[] = {"[a-z]+@[a-z]+", R"(\w+-\w+)", R"(\d+\.\d+)", R"((\w+)@(\w+))",
                            "[a-z]+-[a-z]+", R"(\w+@\w+)", R"([A-Za-z]+:[A-Za-z]+)", "[^ ]+/[^ ]+"};
  const char* subjects[] = {"a@b", "@", "@@@", "ab@", "@cd", "a@b@c", "aa@bb cc@dd", "x.y.z", "1.2.3.4",
                            "..", "a..b", "élève@école", "naïve-café", "日本-語", "a/b//c/", "-a-b-",
                            "9.9", "z-", "-z", "", " ", "@a@b@c@d@", "a-b-c-d-e-f-g", "ééé@ààà",
                            "x@\xC3", "\x80@x", "aaaa@bbbb", "the quick fox@localhost 12.5 over-under, "};

  std::size_t compared {0};
  for (const char* pattern : patterns) {
    const real::regex re {pattern};
    for (const char* subject : subjects) {
      const std::string_view text {subject};

      real::detail::inner_literal_route_disabled() = false;
      std::vector<std::pair<std::size_t, std::size_t>> routed;
      for (const auto& m : re.find_iter(text)) {
        routed.emplace_back(m.start(0), m.end(0));
      }

      real::detail::inner_literal_route_disabled() = true;
      std::vector<std::pair<std::size_t, std::size_t>> core;
      for (const auto& m : re.find_iter(text)) {
        core.emplace_back(m.start(0), m.end(0));
      }
      real::detail::inner_literal_route_disabled() = false;

      EXPECT(routed == core);
      ++compared;
    }
  }
  EXPECT_EQ(compared, sizeof(patterns) / sizeof(*patterns) * (sizeof(subjects) / sizeof(*subjects)));
}

TEST(static_regex_reverse_by_class_matches_the_dynamic_regex)
{
  // The point of the whole thing: a storage with no per-regex cache now places its own match starts, so its
  // spans must equal the dynamic regex's on the shapes that route this way.
  const std::string text = [] {
                             std::string t;
                             for (int i = 0; i < 30; ++i) {
                               t += "on 2026-06-10 a-b root@localhost paid 19.99 then x9 élève@école, ";
                             }
                             return t;
                           }();

  const auto compare {[&text](const auto& stat, const char* pattern) {
                        const real::regex                                dyn {pattern};
                        std::vector<std::pair<std::size_t, std::size_t>> from_static;
                        std::vector<std::pair<std::size_t, std::size_t>> from_dynamic;
                        for (const auto& m : stat.find_iter(text)) {
                          from_static.emplace_back(m.start(0), m.end(0));
                        }
                        for (const auto& m : dyn.find_iter(text)) {
                          from_dynamic.emplace_back(m.start(0), m.end(0));
                        }
                        EXPECT(!from_static.empty());
                        EXPECT(from_static == from_dynamic);
                      }};

  compare(real::static_regex<"[a-z]+@[a-z]+"> {}, "[a-z]+@[a-z]+");
  compare(real::static_regex<R"(\w+-\w+)"> {}, R"(\w+-\w+)");
  compare(real::static_regex<R"(\d+\.\d+)"> {}, R"(\d+\.\d+)");
  compare(real::static_regex<R"((\w+)@(\w+))"> {}, R"((\w+)@(\w+))");
}

//! IL.7: the two-run confirm. When the WHOLE pattern is `class+ <literal> class+`, a candidate whose start
//! the backward walk already placed needs no match engine to finish: the end is where the suffix run stops,
//! and every capture slot is one of four positions. These pin which patterns qualify — the risk is a shape
//! that merely LOOKS like it, where a direct walk would report a match the pattern does not have.
TEST(inner_literal_two_run_confirm_fires_on_exactly_the_two_run_shapes)
{
  const auto fwd_class {[](const char* pattern) {
                          const real::detail::ast tree {real::detail::parse(pattern, real::flags::none)};
                          return real::detail::compile(tree, real::flags::none | tree.inline_flags).hints.il_fwd_class;
                        }};

  EXPECT(fwd_class("[a-z]+@[a-z]+") >= 0);
  EXPECT(fwd_class(R"(\w+-\w+)") >= 0);
  EXPECT(fwd_class(R"(\d+\.\d+)") >= 0);
  EXPECT(fwd_class(R"((\w+)@(\w+))") >= 0); // capture groups around either run are transparent

  // Anything after the suffix run disqualifies it: the walk would run to the end of the class run and
  // report a match, where the pattern still has work to do.
  EXPECT_EQ(fwd_class(R"(\w+@\w+\.\w+)"), -1);      // a second literal follows
  EXPECT_EQ(fwd_class("[a-z]+@[a-z]+x"), -1);       // a trailing byte
  EXPECT_EQ(fwd_class(R"(\w+@\w+\b)"), -1);         // a trailing assertion
  EXPECT_EQ(fwd_class("[a-z]+@[a-z]+?"), -1);       // a LAZY suffix does not run to the end of the class run
  EXPECT_EQ(fwd_class(R"(\d{4}-\d{2}-\d{2})"), -1); // fixed counts, not loops
  EXPECT_EQ(fwd_class("[a-z]+"), -1);               // no inner literal
}

TEST(inner_literal_two_run_confirm_fills_the_same_captures_as_the_core)
{
  // The direct path writes capture slots by anchor instead of running the VM, so every group of every match
  // is compared against the same engine with the route switched off — not just the whole-match span.
  const char* patterns[] = {R"((\w+)@(\w+))", "([a-z]+)-([a-z]+)", R"((\d+)\.(\d+))", R"(((\w+))@(\w+))",
                            R"(\w+@(\w+))",   R"((\w+)@\w+)"};
  const char* subjects[] = {"root@localhost", "a-b", "19.99", "x@y", "élève@école", "ab-cd ef-gh",
                            "1.2 33.44", "@", "a@", "@b", "", "a@b@c", "aaa@bbb ccc@ddd eee@fff"};

  for (const char* pattern : patterns) {
    const real::regex re {pattern};
    for (const char* subject : subjects) {
      const std::string_view text {subject};

      const auto collect          {[&] {
                                     std::vector<std::pair<std::size_t, std::size_t>> all;
                                     for (const auto& m : re.find_iter(text)) {
                                       for (std::size_t g = 0; g <= re.group_count(); ++g) {
                                         all.emplace_back(m.start(g), m.end(g));
                                       }
                                     }
                                     return all;
                                   }};

      real::detail::inner_literal_route_disabled() = false;
      const auto routed {collect()};
      real::detail::inner_literal_route_disabled() = true;
      const auto core   {collect()};
      real::detail::inner_literal_route_disabled() = false;

      EXPECT(routed == core);
    }
  }
}

//! IL.8: the fixed code-point shape. `il_fused_eligible` covers a sequence whose BYTE width is fixed; a
//! `klass_cp` never is (a Unicode `\d` matches multi-byte digits), but its code-point COUNT still is. These
//! pin which patterns qualify — a shape wrongly admitted here would have its start placed by counting code
//! points that the pattern does not in fact require.
TEST(inner_literal_fixed_codepoint_shape_fires_on_exactly_the_loopless_sequences)
{
  const auto shape {[](const char* pattern) {
                      const real::detail::ast tree {real::detail::parse(pattern, real::flags::none)};
                      const auto              prog {real::detail::compile(tree, real::flags::none | tree.inline_flags)};
                      return std::pair<bool, unsigned> {prog.hints.il_cp_shape_eligible,
                                                        static_cast<unsigned>(prog.hints.il_cp_prefix_cps)};
                    }};

  EXPECT(shape(R"(\d{4}-\d{2}-\d{2})").first);
  EXPECT_EQ(shape(R"(\d{4}-\d{2}-\d{2})").second, 4U); // four code points before the first `-`
  EXPECT(shape(R"(\w{3}:\w{3})").first);
  EXPECT_EQ(shape(R"(\w{3}:\w{3})").second, 3U);

  // Any loop disqualifies it: a `+` makes the count before the literal variable, so a fixed step back
  // would land somewhere the pattern never said.
  EXPECT(!shape(R"(\d+\.\d+)").first);
  EXPECT(!shape("[a-z]+@[a-z]+").first);
  EXPECT(!shape(R"(\d{4}-\d{2}-\d*)").first);
  EXPECT(!shape(R"(\d{2,4}-\d{2})").first); // a bounded repeat still compiles to a split
  EXPECT(!shape(R"(\w{3}:\w{3}\b)").first); // a trailing assertion is not a consuming atom
  EXPECT(!shape("dog").first);              // literal at the head, no prefix to step back over
}

TEST(inner_literal_fixed_codepoint_shape_matches_the_core)
{
  // Differential against the same engine with the route off, on the shapes the walk now serves and at the
  // edges a fixed step back would get wrong: a candidate too close to the subject start, a multi-byte
  // member inside the prefix (so the step back is not a byte count), repeated and adjacent literals.
  const char* patterns[] = {R"(\d{4}-\d{2}-\d{2})", R"(\w{3}:\w{3})", R"(\d{2}/\d{2})", R"((\d{4})-(\d{2}))"};
  const char* subjects[] = {"2026-06-10", "x2026-06-10y", "999-99-99", "ab:cd", "abc:def", "12/34 56/78",
                            "élè:ves", "2026-06-1", "-2026-06-10", "--", "", "1", "2026-06-10 1999-01-02",
                            "\xC3\xA9\xC3\xA9\xC3\xA9:abc", "0000-00-00-00"};

  for (const char* pattern : patterns) {
    const real::regex re {pattern};
    for (const char* subject : subjects) {
      const std::string_view text {subject};

      const auto collect          {[&] {
                                     std::vector<std::pair<std::size_t, std::size_t>> all;
                                     for (const auto& m : re.find_iter(text)) {
                                       for (std::size_t g = 0; g <= re.group_count(); ++g) {
                                         all.emplace_back(m.start(g), m.end(g));
                                       }
                                     }
                                     return all;
                                   }};

      real::detail::inner_literal_route_disabled() = false;
      const auto routed {collect()};
      real::detail::inner_literal_route_disabled() = true;
      const auto core   {collect()};
      real::detail::inner_literal_route_disabled() = false;

      EXPECT(routed == core);
    }
  }
}
