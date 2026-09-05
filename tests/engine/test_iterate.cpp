// find_iter / find_all / replace / split, with Python's empty-match rules
// (verified against re: spans, sub and split outputs are identical).
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(find_all_basic)
{
  const real::regex rx("\\d+");
  const auto        all = rx.find_all("a1 bb22 c333");
  EXPECT_EQ(all.size(), 3U);
  EXPECT_EQ(all[0][0], "1"sv);
  EXPECT_EQ(all[1][0], "22"sv);
  EXPECT_EQ(all[2][0], "333"sv);
  EXPECT_EQ(all[2].start(), 9U);
  EXPECT_EQ(rx.find_all("none").size(), 0U);
}

TEST(match_iterator_equality_identifies_the_WALK_not_just_the_offset)
{
  // `operator==` compared `done_` and `pos_` and nothing else, so two iterators over DIFFERENT
  // walks compared equal whenever they happened to sit at the same offset -- while dereferencing to
  // different text. Nothing tested equality at all, which is how a public contract stays wrong.
  //
  // The sentinel half constrains the fix and is asserted first: `end()` is a default-constructed
  // iterator, so "both exhausted" must stay equal REGARDLESS of walk, or every range-for over this
  // type loops forever. The identity check therefore belongs only on the live branch.
  const std::string x_text {"aXbXc"};
  const std::string y_text {"aYbYc"};
  const real::regex rx     {"X"};
  const real::regex ry     {"Y"};

  const auto x_range = rx.find_iter(x_text);
  const auto y_range = ry.find_iter(y_text);
  auto       a       = x_range.begin();
  auto       b       = y_range.begin();
  EXPECT(a->start(0) == b->start(0));   // the coincidence the old comparison mistook for identity
  EXPECT(a->str(0) != b->str(0));       // and they read different text, so they are not the same
  EXPECT(!(a == b));
  EXPECT(a != b);

  // One axis at a time: same pattern over a different text, and a different pattern over the same
  // text. Either alone must be enough to tell two walks apart.
  const std::string other_x {"aXbXd"};
  const auto        other_range = rx.find_iter(other_x);
  EXPECT(a != other_range.begin());

  const real::regex any {"[XY]"};
  const auto        any_range = any.find_iter(x_text);
  EXPECT(a != any_range.begin());

  // Same walk, same position: still equal. A comparison that told everything apart would be as
  // useless as one that told nothing apart.
  auto a_again = x_range.begin();
  EXPECT(a == a_again);
  ++a_again;
  EXPECT(a != a_again);                 // and advancing separates them

  // The sentinel contract, both directions, on both walks.
  auto walked = x_range.begin();
  while (walked != x_range.end()) {
    ++walked;
  }
  EXPECT(walked == x_range.end());
  EXPECT(walked == y_range.end());      // end() is default-constructed: one sentinel for every walk

  auto y_walked = y_range.begin();
  while (y_walked != y_range.end()) {
    ++y_walked;
  }
  EXPECT(walked == y_walked);           // two EXHAUSTED iterators are both the sentinel value
}

TEST(find_iter_is_lazy_and_range_for_compatible)
{
  const real::regex rx("[ab]");
  std::string       seen;
  for (const auto& match : rx.find_iter("xaybz")) {
    seen += match[0];
  }
  EXPECT_EQ(seen, std::string("ab"));
}

TEST(find_iter_with_captures_and_anchors)
{
  const real::regex rx("^(\\w+)", real::flags::multiline);
  const auto        all = rx.find_all("foo x\nbar y");
  EXPECT_EQ(all.size(), 2U);
  EXPECT_EQ(all[0][1], "foo"sv);
  EXPECT_EQ(all[1][1], "bar"sv);
}

TEST(temporary_regex_cannot_dangle_in_iteration)
{
  // find_iter / find_all on a temporary regex are deleted overloads: in a
  // C++20 range-for the temporary would die before the loop body. This
  // must not compile (caught originally by the constexpr interpreter):
  //   for (auto& match : real::regex("x").find_iter(text)) { ... }
  const real::regex rx("x");
  EXPECT(rx.find_iter("x").begin() != rx.find_iter("x").end());
}

TEST(empty_matches_python_spans)
{
  // Python: [(0,0), (1,2), (2,2), (3,3)] — empty right after non-empty.
  const real::regex rx("x*");
  const auto        all = rx.find_all("axb");
  EXPECT_EQ(all.size(), 4U);
  EXPECT_EQ(all[0].start(), 0U);
  EXPECT_EQ(all[0].end(), 0U);
  EXPECT_EQ(all[1].start(), 1U);
  EXPECT_EQ(all[1].end(), 2U);
  EXPECT_EQ(all[2].start(), 2U);
  EXPECT_EQ(all[2].end(), 2U);
  EXPECT_EQ(all[3].start(), 3U);
  EXPECT_EQ(all[3].end(), 3U);
}

TEST(empty_matches_advance_whole_codepoints)
{
  // '.' over "héo": 3 matches, never splitting the 2-byte é.
  const real::regex dot(".");
  const auto        dots = dot.find_all("héo");
  EXPECT_EQ(dots.size(), 3U);
  EXPECT_EQ(dots[1][0], "é"sv);
  // Empty matches between codepoints: 3 positions, like Python.
  const real::regex xs("x*");
  EXPECT_EQ(xs.find_all("éo").size(), 3U);
}

// CI finding (python differential macos-3.10 + posix macos-clang pin, 2026-07-17):
// empty-leading alternation + quasi-shorthand class with EURO (U+20AC → cp_hi path).
// Root cause of the C++ pin failure (4 spans, bogus [0,7) "€😀"): thread-local cp_hi
// cache keyed by a *pointer into a dead program*'s cp_ranges; after many classes the
// allocator reuses that address for a different class and membership returns the wrong
// sparse table (emoji false-positive under [\w€]). Fixed in pike.hpp: content fingerprint.
//
// Engine offsets are BYTE indices. Subject "€😀é €€x" is 17 UTF-8 bytes; mid "€€" is [10,16).
// Both UTF-8 source form and \u/\x form are exercised — the bug is in the cache, not charset.
TEST(find_iter_euro_class_empty_alt_keeps_mid_match)
{
  const real::flags fl        {real::flags::multiline | real::flags::dotall};
  const std::string text_utf8 {"€😀é €€x"};
  const std::string text_hex  {"\xE2\x82\xAC\xF0\x9F\x98\x80\xC3\xA9 \xE2\x82\xAC\xE2\x82\xACx"};
  EXPECT_EQ(text_utf8, text_hex);
  EXPECT_EQ(text_utf8.size(), 17U);

  auto check_spans = [&](const real::regex& rx, const std::string& text) {
                       std::vector<std::pair<std::size_t, std::size_t>> spans;
                       for (const auto& m : rx.find_iter(text)) {
                         spans.emplace_back(m.start(), m.end());
                       }
                       EXPECT_EQ(spans.size(), 3U);
                       if (spans.size() != 3U) {
                         return;
                       }
                       EXPECT_EQ(spans[0].first, 0U);
                       EXPECT_EQ(spans[0].second, 0U);
                       EXPECT_EQ(spans[1].first, 10U);
                       EXPECT_EQ(spans[1].second, 16U);
                       EXPECT_EQ(text.substr(10, 6), std::string {"\xE2\x82\xAC\xE2\x82\xAC"});
                       EXPECT_EQ(spans[2].first, 17U);
                       EXPECT_EQ(spans[2].second, 17U);
                     };

  // UTF-8-in-source form (the shape that failed on macos-clang before the cache fix).
  check_spans(real::regex {R"(^|[\w€]{2}|\137?[é]??$)", fl}, text_utf8);
  // Escape form (same membership, independent of source charset).
  check_spans(real::regex {R"(^|[\w\u20AC]{2}|\137?[\u00E9]??$)", fl}, text_hex);

  // 😀 must never be a member of [\w€] (the false positive the dangling cache produced).
  EXPECT(!real::regex {R"([\w€])"}.search("😀").matched());
  EXPECT(!real::regex {R"([\w€]{2})"}.search("€😀").matched());

  // Stress: recompile + iterate; aggregate wrong-count (no 1000-line FAIL spam).
  std::size_t wrong {};
  for (int i = 0; i < 1000; ++i) {
    const real::regex again {R"(^|[\w€]{2}|\137?[é]??$)", fl};
    std::size_t       n     {};
    for (const auto& m : again.find_iter(text_utf8)) {
      EXPECT(m.start() <= m.end());
      ++n;
    }
    if (n != 3U) {
      ++wrong;
    }
  }
  EXPECT_EQ(wrong, 0U);
}

// Deterministic repro of the cp_hi pointer-key UAF: pollute the thread-local sparse
// cache with a high-range class that *does* contain emoji, destroy it, then probe [\w€]
// — with a pointer key the recycled address can return the old table and 😀 falsely matches.
TEST(cp_hi_cache_not_poisoned_by_destroyed_program)
{
  {
    // \p{So} (Symbol, other) includes U+1F600 GRINNING FACE and is dense enough for cp_hi.
    const real::regex poison {R"(\p{So}+)"};
    EXPECT(poison.search("😀").matched());
  } // poison destroyed — its cp_ranges storage may be reused
  // Fresh class: word + euro only. Must not inherit So membership via a stale cache hit.
  const real::regex word_euro {R"([\w€]+)"};
  EXPECT(!word_euro.search("😀").matched());
  EXPECT(word_euro.search("€").matched());
  EXPECT(word_euro.search("ab").matched());
  // The original find_iter shape after pollution.
  const real::flags fl   {real::flags::multiline | real::flags::dotall};
  const real::regex rx   {R"(^|[\w€]{2}|\137?[é]??$)", fl};
  const std::string text {"€😀é €€x"};
  std::size_t       n    {};
  for (const auto& m : rx.find_iter(text)) {
    (void) m;
    ++n;
  }
  EXPECT_EQ(n, 3U);
}

TEST(replace_basic_and_count)
{
  const real::regex rx("\\d+");
  EXPECT_EQ(rx.replace("a1b22c", "#"), std::string("a#b#c"));
  EXPECT_EQ(rx.replace("a1b22c", "#", 1), std::string("a#b22c"));
  EXPECT_EQ(rx.replace("none", "#"), std::string("none"));
  // Python: re.sub('x*', '-', 'axb') == '-a--b-'
  EXPECT_EQ(real::regex("x*").replace("axb", "-"), std::string("-a--b-"));
}

TEST(replace_group_references)
{
  const real::regex rx("(\\w+)@(\\w+)");
  EXPECT_EQ(rx.replace("bob@host", "$2:$1"), std::string("host:bob"));
  EXPECT_EQ(rx.replace("bob@host", "$&!"), std::string("bob@host!"));
  EXPECT_EQ(rx.replace("bob@host", "$0!"), std::string("bob@host!"));
  EXPECT_EQ(rx.replace("bob@host", "$$$1"), std::string("$bob"));
  const real::regex named("(?P<user>\\w+)@(?P<host>\\w+)");
  EXPECT_EQ(named.replace("bob@host", "${host}/${user}"), std::string("host/bob"));
  // Unset group expands to nothing.
  EXPECT_EQ(real::regex("(a)|(b)").replace("b", "[$1$2]"), std::string("[b]"));
}

TEST(replace_errors)
{
  const real::regex rx("(a)");
  EXPECT_THROWS(rx.replace("a", "$"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "$2"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "$x"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "${nope}"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "${}"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "${a"), real::regex_error);
}

TEST(split_python_semantics)
{
  const real::regex comma(",");
  const auto        parts = comma.split("a,b,,c");
  EXPECT_EQ(parts.size(), 4U);
  EXPECT_EQ(parts[0], "a"sv);
  EXPECT_EQ(parts[2], ""sv);
  EXPECT_EQ(parts[3], "c"sv);
  // With a capturing group, the separator is kept: ['a', ',', 'b,c'].
  const auto kept = real::regex("(,)").split("a,b,c", 1);
  EXPECT_EQ(kept.size(), 3U);
  EXPECT_EQ(kept[1], ","sv);
  EXPECT_EQ(kept[2], "b,c"sv);
  // Python: re.split('x*', 'axb') == ['', 'a', '', 'b', ''].
  const auto empties = real::regex("x*").split("axb");
  EXPECT_EQ(empties.size(), 5U);
  EXPECT_EQ(empties[1], "a"sv);
  EXPECT_EQ(empties[3], "b"sv);
  // No match: one piece, the whole text.
  EXPECT_EQ(comma.split("abc").size(), 1U);
}

TEST(iteration_state_is_reusable)
{
  const real::regex rx("a");
  const std::string text(1000, 'a');
  EXPECT_EQ(rx.find_all(text).size(), 1000U);
  EXPECT_EQ(rx.replace(text, "b"), std::string(1000, 'b'));
}

TEST(empty_match_then_nonempty_at_same_position_cpython37)
{
  // Regression (found by differential fuzzing vs re): CPython 3.7+ allows a
  // non-empty match to start right after an empty match at the SAME position.
  // \W??\b on "cbAAA.A__A": after the empty (5,5), the non-empty (5,6)
  // (consuming '.') must appear before advancing — it used to be skipped.
  const real::regex     rx("\\W??\\b");
  const auto            all = rx.find_all("cbAAA.A__A");
  EXPECT_EQ(all.size(), 5U);
  EXPECT_EQ(all[1].start(), 5U);
  EXPECT_EQ(all[1].end(), 5U); // empty
  EXPECT_EQ(all[2].start(), 5U);
  EXPECT_EQ(all[2].end(), 6U); // non-empty, same start
  EXPECT_EQ(all[3].start(), 6U);
  // The skip stays codepoint-aligned: x* over "é" yields two matches, never
  // a match starting inside the 2-byte sequence.
  const real::regex star("x*");
  EXPECT_EQ(star.find_all("é").size(), 2U);
}

TEST(find_iter_models_forward_iterator)
{
  const real::regex rx("\\d+");
  using iter_t = decltype(rx.find_iter("").begin());
  static_assert(std::forward_iterator<iter_t>); // conformance: post-increment + multipass

  // it++ returns the pre-increment position (an independent copy); iteration then continues.
  auto       range {rx.find_iter("a1 b22 c333")};
  auto       it    {range.begin()};
  const auto first {it++};
  EXPECT_EQ((*first)[0], "1"sv);  // the copy kept the old position
  EXPECT_EQ((*it)[0], "22"sv);    // *this advanced
  ++it;
  EXPECT_EQ((*it)[0], "333"sv);
}

// The iterator refreshes one match in place across steps (reusing its slot buffer, the per-match diet),
// so a saved COPY of an earlier match must stay independent of later ones. Collect every match by value
// during iteration and check them all afterwards — the copies must not alias the reused buffer.
TEST(find_iter_copies_survive_the_reused_buffer)
{
  const real::regex                                   rx("(\\w)(\\d+)");
  auto                                                range {rx.find_iter("a1 b22 c333")};
  std::vector<std::decay_t<decltype(*range.begin())>> saved;
  for (const auto& match : range) {
    saved.push_back(match); // a by-value copy — must own its slots
  }
  EXPECT_EQ(saved.size(), std::size_t {3});
  EXPECT_EQ(saved[0][0], "a1"sv);
  EXPECT_EQ(saved[0][1], "a"sv);
  EXPECT_EQ(saved[0][2], "1"sv);
  EXPECT_EQ(saved[1][0], "b22"sv);
  EXPECT_EQ(saved[1][2], "22"sv);
  EXPECT_EQ(saved[2][0], "c333"sv);
  EXPECT_EQ(saved[2][2], "333"sv);
}
