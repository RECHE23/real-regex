// `\A`/`^` is peeled by the shape recognizers and honoured as a MODE by the routes they arm
// (prefilter.hpp's shape_lead::anchored_start, pike.hpp's anchored_mode). That is two pieces of
// machinery that must agree, in a place where disagreeing is silent: a forward-scanning route armed
// for a pattern pinned to position 0 returns matches the program forbids, and every existing test
// still passes because they use unanchored patterns.
//
// The walk cases are not decoration. The batched walk bypasses run() -- which is where the anchor
// becomes prefix anchoring -- so it must refuse anchored shapes, and it did not until a spans-level
// differential caught `^[a-z]+` reporting a match at offset 2 of "  abc".
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/automata/lazy_dfa.hpp" // class_fastpath_disabled
#include "real/real.hpp"

namespace {

  std::vector<std::pair<std::size_t, std::size_t>> walk(const real::regex& rx,
                                                        std::string_view   t)
  {
    std::vector<std::pair<std::size_t, std::size_t>> v;
    for (const auto& m : rx.find_iter(t)) {
      v.emplace_back(m.start(0), m.end(0));
    }
    return v;
  }
} // namespace

TEST(anchored_class_shape_matches_only_at_zero)
{
  for (const char* pat : {"^[a-z]+", "\\A[a-z]+"}) {
    const real::regex rx {pat};
    EXPECT(rx.search(std::string_view {"abc def"}).matched());
    EXPECT(rx.search(std::string_view {"abc def"}).start(0) == 0);
    EXPECT(rx.search(std::string_view {"abc def"}).end(0) == 3);
    EXPECT(!rx.search(std::string_view {"  abc"}).matched());   // the run exists, but not at 0
    EXPECT(!rx.search(std::string_view {"123abc"}).matched());
    EXPECT(!rx.search(std::string_view {"\nabc"}).matched());   // not multiline: `^` is the SUBJECT start
    EXPECT(!rx.search(std::string_view {""}).matched());
    // A region that begins past 0 can never hold the match, whatever it contains.
    EXPECT(!rx.search(std::string_view {"abc def"}, 1).matched());
    EXPECT(!rx.search(std::string_view {"xabc"}, 1).matched());
  }
}

TEST(anchored_class_shape_yields_one_match_per_walk)
{
  const real::regex rx                                {"^[a-z]+"};
  EXPECT(walk(rx, "abc def").size() == 1);
  const std::pair<std::size_t, std::size_t> first_run {0, 3};
  EXPECT(walk(rx, "abc def")[0] == first_run);
  EXPECT(walk(rx, "abc\ndef").size() == 1);
  EXPECT(walk(rx, "aaa,bbb").size() == 1);
  EXPECT(walk(rx, "  abc").empty());
  EXPECT(walk(rx, "123abc").empty());
}

TEST(anchored_codepoint_class_shape)
{
  const real::regex rx {"^\\w+"};
  EXPECT(rx.search(std::string_view {"abc def"}).matched());
  EXPECT(rx.search(std::string_view {"abc def"}).end(0) == 3);
  EXPECT(!rx.search(std::string_view {" abc"}).matched());
  EXPECT(walk(rx, "abc def").size() == 1);
}

TEST(multiline_caret_is_not_peeled)
{
  // Multiline `^` is a DIFFERENT assertion (line start), so the peel must not apply and the walk
  // must still find a run after every newline.
  const real::regex rx {"^[a-z]+", real::flags::multiline};
  EXPECT(walk(rx, "abc\ndef").size() == 2);
  EXPECT(walk(rx, "abc\ndef")[1].first == 4);
  EXPECT(rx.search(std::string_view {"\nabc"}).matched());
}

TEST(anchored_shape_agrees_with_the_general_route)
{
  // The seam differential in miniature: forcing the shape routes out must not change an answer.
  for (const char* pat : {"^[a-z]+", "^\\w+", "^[0-9]+", "^[^,]+"}) {
    for (const char* subj : {"", "a", "abc def", "  abc", "123abc", "abc\ndef", "aaa,bbb"}) {
      real::detail::class_fastpath_disabled() = false;
      const auto fast    {walk(real::regex {pat}, subj)};
      real::detail::class_fastpath_disabled() = true;
      const auto general {walk(real::regex {pat}, subj)};
      real::detail::class_fastpath_disabled() = false;
      EXPECT(fast == general);
    }
  }
}

TEST(end_anchored_class_shape_matches_only_at_the_limit)
{
  const real::regex rx {"[a-z]+$"};
  // The leftmost match is the run that ENDS at the anchor, not the first run in the subject.
  EXPECT(rx.search(std::string_view {"abc def"}).matched());
  EXPECT(rx.search(std::string_view {"abc def"}).start(0) == 4);
  EXPECT(rx.search(std::string_view {"abc def"}).end(0) == 7);
  EXPECT(!rx.search(std::string_view {"abc123"}).matched()); // the run does not reach the end
  EXPECT(!rx.search(std::string_view {""}).matched());
  // `match` is prefix-anchored AND end-anchored: the run must span from the start to the limit.
  EXPECT(!rx.match(std::string_view {"abc def"}).matched());
  EXPECT(rx.match(std::string_view {"abc"}).matched());
}

TEST(dollar_accepts_one_final_newline_but_backslash_z_does_not)
{
  // This is why `^X$` is NOT fullmatch(X), and the difference is silent if the limit is computed
  // wrong: `$` also matches just before ONE final newline; `\Z` is the strict end.
  const real::regex dollar {"^[a-z]+$"};
  const real::regex strict {"^[a-z]+\\Z"};
  EXPECT(dollar.search(std::string_view {"abc"}).matched());
  EXPECT(dollar.search(std::string_view {"abc\n"}).matched());
  EXPECT(dollar.search(std::string_view {"abc\n"}).end(0) == 3);  // ends BEFORE the newline
  EXPECT(!dollar.search(std::string_view {"abc\n\n"}).matched()); // only one
  EXPECT(strict.search(std::string_view {"abc"}).matched());
  EXPECT(!strict.search(std::string_view {"abc\n"}).matched());
  // fullmatch spans the WHOLE subject, so the trailing newline is not excusable there.
  EXPECT(!real::regex {"[a-z]+"}.fullmatch(std::string_view {"abc\n"}).matched());
}

TEST(end_anchored_shape_yields_one_match_per_walk)
{
  const real::regex                                rx {"[a-z]+$"};
  std::vector<std::pair<std::size_t, std::size_t>> v;
  for (const auto& m : rx.find_iter(std::string_view {"abc def"})) {
    v.emplace_back(m.start(0), m.end(0));
  }
  EXPECT(v.size() == 1);
  const std::pair<std::size_t, std::size_t> tail_run {4, 7};
  EXPECT(v[0] == tail_run);
}

TEST(end_anchored_shape_agrees_with_the_general_route)
{
  for (const char* pat : {"[a-z]+$", "^[a-z]+$", "[a-z]+\\Z", "^[a-z]+\\Z", "[0-9]+$", "[a-z]{2,}$"}) {
    for (const char* subj : {"", "a", "abc", "abc def", "abc\n", "abc\n\n", "  abc", "123abc",
                             "abc123", "abc\ndef"}) {
      real::detail::class_fastpath_disabled() = false;
      const auto fast    {walk(real::regex {pat}, subj)};
      real::detail::class_fastpath_disabled() = true;
      const auto general {walk(real::regex {pat}, subj)};
      real::detail::class_fastpath_disabled() = false;
      EXPECT(fast == general);
    }
  }
}

TEST(multiline_dollar_is_not_peeled)
{
  const real::regex rx {"[a-z]+$", real::flags::multiline};
  EXPECT(walk(rx, "abc\ndef").size() == 2);
  EXPECT(walk(rx, "abc\ndef")[0].second == 3);
}

TEST(anchored_codepoint_class_end_and_both)
{
  // The code-point route honours the same two anchors as the byte one; `^\w+$` was the last shape
  // still on the one-pass DFA (1.450 -> 0.065 ns/B over 100 KB).
  const real::regex both {"^\\w+$"};
  EXPECT(both.search(std::string_view {"abc"}).matched());
  EXPECT(both.search(std::string_view {"abc\n"}).matched());        // `$` takes one final newline
  EXPECT(!both.search(std::string_view {"abc def"}).matched());     // must span start to limit
  EXPECT(!both.search(std::string_view {" abc"}).matched());
  const real::regex tail {"\\w+$"};
  EXPECT(tail.search(std::string_view {"abc def"}).start(0) == 4);  // the run ENDING at the limit
  EXPECT(!tail.search(std::string_view {"abc ,"}).matched());
}

TEST(word_boundary_with_an_end_anchor_is_not_peeled)
{
  // `\b` takes its own branch in the routes and that branch never sees an end limit, so the peel is
  // refused rather than combined -- a real divergence on `[a-z]+\b$` before that condition existed.
  for (const char* pat : {"[a-z]+\\b$", "\\w+\\b$", "^[a-z]+\\b$"}) {
    for (const char* subj : {"abc def", "abc", "abc ", "", "a"}) {
      real::detail::class_fastpath_disabled() = false;
      const auto fast    {walk(real::regex {pat}, subj)};
      real::detail::class_fastpath_disabled() = true;
      const auto general {walk(real::regex {pat}, subj)};
      real::detail::class_fastpath_disabled() = false;
      EXPECT(fast == general);
    }
  }
}

// `fixed_shape` now KEEPS an anchored pattern instead of refusing it, and this is the differential that
// makes that safe. It refused them until measurement showed what the refusal cost: every anchored
// fixed-width shape -- the dominant form in config parsing, validation and log scanning -- fell to the
// general Pike VM. On x86-64/libstdc++, `^[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}$` over a
// 19-byte subject went from 628.7 ns per call to 186.6, which turns a 3.1x LOSS against `std::regex` into
// a 1.7x win; the per-call rows in benchmarks/bench_minimal.cpp read 458 -> 42 ns.
//
// TWO bugs had to be fixed to get there, and both are the reason this test generates its cases instead of
// listing a few:
//
//   * `body_pc` was only advanced past a peeled `\b`, never past a peeled `^`. The verify then began its
//     walk ON the assert, stopped there, and reported a ZERO-WIDTH match -- so `^[0-9]{4}-…$` "matched" an
//     empty subject and `find_iter` yielded that match forever.
//   * `fixed_shape_pair`, the SIMD two-position prefilter, is armed by the same recognizer and its gate
//     runs BEFORE fixed_shape's. It documents itself as transparent -- "it only filters candidates" --
//     and that contract holds only while the shape may start anywhere. With an anchor peeled it returned
//     candidates the assertion forbids: `^[0-9]{4}-[0-9]{2}-[0-9]{2}$` reported [0,10) on
//     "2026-08-10_11:43:27". It now refuses a peeled anchor, which costs nothing that matters -- a
//     prefilter exists to SKIP candidate positions and an anchored shape has exactly one.
//
// Neither bug was a wrong answer the suite could see any other way: both routes are correct by
// construction on unanchored input, so only routed-vs-core on ANCHORED input distinguishes them.
TEST(fixed_shape_anchored_routed_equals_core)
{
  const std::vector<std::string> bodies {"[0-9]{2}", "[0-9]{4}-[0-9]{2}", "ab", "a[bc]d",
                                         "[a-z]{3}_[0-9]{2}", "[0-9]{2}:[0-9]{2}", "[ab][cd][ef]"};
  const std::vector<std::string> leads  {"", "^", R"(\A)", R"(\b)"};
  const std::vector<std::string> tails  {"", "$", R"(\Z)", R"(\b)"};
  // The subjects that matter are the EDGES: empty, a bare newline, `$` before a final newline, one byte
  // short, one byte long, and the match with text after it (the case fixed_shape_pair got wrong).
  const std::vector<std::string> subs {"",        "\n",       "12",     "12\n",   "12\n\n", "1234-56",
                                       "1234-56\n", "ab",     "ab\n",   "abd",    "acd",    "acd\n",
                                       "abc_12",  "abc_12\n", "11:22",  "11:22\n", "ace",   "ace\n",
                                       " 12 ",    "z12z",     "1234-56-78", "prefixe 12 suffixe"};
  bool saw_armed {false};
  for (const auto& b : bodies) {
    for (const auto& l : leads) {
      for (const auto& t : tails) {
        std::string pat {l}; // built with += : clang-tidy's performance-inefficient-string-concatenation
        pat += b;            // fires on `l + b + t`, and the gate treats it as an error
        pat += t;
        const real::regex probe {pat};
        // An anchored shape that is armed is the point; record that at least one is, so a recognizer
        // change that silently stops arming them cannot leave this test passing vacuously.
        if (probe.raw_program().hints.fixed_shape && probe.raw_program().hints.anchored_start) {
          saw_armed = true;
          // The pair prefilter must be OFF whenever an anchor was peeled -- see the note above.
          EXPECT_EQ(static_cast<int>(probe.raw_program().hints.fs_pair_width), 0);
        }
        for (const auto& s : subs) {
          // Each arm builds its OWN regex: the seam is applied once per regex at construction
          // (storage.hpp), so one compiled before the toggle would carry the routed hints into both arms.
          real::detail::fixed_shape_route_disabled() = true;
          const real::regex off    {pat};
          const auto        core   {walk(off, s)};
          real::detail::fixed_shape_route_disabled() = false;
          const real::regex on     {pat};
          const auto        routed {walk(on, s)};
          EXPECT_EQ(routed.size(), core.size());
          if (routed.size() == core.size()) {
            for (std::size_t i {0}; i < core.size(); ++i) {
              EXPECT_EQ(routed[i].first, core[i].first);
              EXPECT_EQ(routed[i].second, core[i].second);
            }
          }
        }
      }
    }
  }
  real::detail::fixed_shape_route_disabled() = false;
  EXPECT(saw_armed);
}
