// The inert lazy-DFA cache frame. Nothing routes matching through lazy_dfa yet —
// these tests drive it directly to pin the machinery: the byte-class alphabet, the priority-ordered subset
// construction, the lazy transition cache (hit/miss), the bounded eviction (flush), and the thrash detector.
// The forward-pass semantics and their differential net against Pike arrive when it is wired in.
#include <utility>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"
#include "real/lazy_dfa.hpp"

using real::detail::dynamic_storage;
using real::detail::lazy_dfa;

TEST(lazy_dfa_eligibility)
{
  const auto plain {dynamic_storage::compile("[a-z]+", real::flags::none)};
  EXPECT(lazy_dfa(plain.program.code, plain.program.classes).eligible());        // byte classes: representable

  const auto asserted {dynamic_storage::compile("\\bfoo", real::flags::none)};
  EXPECT(!lazy_dfa(asserted.program.code, asserted.program.classes).eligible()); // \b: position assertion

  const auto uni {dynamic_storage::compile("\\w", real::flags::none)};
  EXPECT(!lazy_dfa(uni.program.code, uni.program.classes).eligible());           // klass_cp: variable-width
}

TEST(lazy_dfa_transitions_a_literal)
{
  const auto st   {dynamic_storage::compile("abc", real::flags::none)};
  lazy_dfa   dfa  {st.program.code, st.program.classes};

  std::uint32_t s {dfa.start_state()};
  EXPECT(!dfa.is_match(s));
  s = dfa.step(s, 'a');
  EXPECT(s != lazy_dfa::dead_state && !dfa.is_match(s));
  s = dfa.step(s, 'b');
  s = dfa.step(s, 'c');
  EXPECT(dfa.is_match(s)); // reached `match`

  // a non-matching byte from the start falls into the dead state, which self-loops
  const std::uint32_t dead {dfa.step(dfa.start_state(), 'z')};
  EXPECT_EQ(dead, lazy_dfa::dead_state);
  EXPECT_EQ(dfa.step(dead, 'a'), lazy_dfa::dead_state);
}

TEST(lazy_dfa_transition_cache_hit_then_miss)
{
  const auto st             {dynamic_storage::compile("[a-z]+", real::flags::none)};
  lazy_dfa   dfa            {st.program.code, st.program.classes};

  const std::uint32_t s     {dfa.start_state()};
  const std::size_t   miss0 {dfa.stats().misses};
  const std::uint32_t once  {dfa.step(s, 'a')};  // first time: a miss (computes + caches)
  EXPECT_EQ(dfa.stats().misses, miss0 + 1);
  const std::size_t   hit0  {dfa.stats().hits};
  const std::uint32_t twice {dfa.step(s, 'a')};  // same edge: a cache hit
  EXPECT_EQ(dfa.stats().hits, hit0 + 1);
  EXPECT_EQ(once, twice);
}

TEST(lazy_dfa_eviction_flushes_and_thrash_trips)
{
  // (a|b)*a(a|b){6} needs ~2^6 states — with a tiny budget the cache must flush repeatedly, and two
  // flushes within one scan must trip the thrash flag (the signal a forward pass uses to fall back).
  const auto st  {dynamic_storage::compile("(a|b)*a(a|b)(a|b)(a|b)(a|b)(a|b)(a|b)", real::flags::none)};
  lazy_dfa   dfa {st.program.code, st.program.classes, /*budget=*/ 4};
  EXPECT(dfa.eligible());

  dfa.begin_scan();
  EXPECT(!dfa.thrashing());
  std::uint32_t s    {dfa.start_state()};
  const char*   text {"abababababababababababab"};
  for (const char* p = text; *p != '\0'; ++p) {
    s = dfa.step(s, static_cast<std::uint8_t>(*p));
    if (s == lazy_dfa::dead_state) {
      s = dfa.start_state(); // a real pass re-seeds; here we just keep driving transitions
    }
  }
  EXPECT(dfa.stats().flushes >= 2);
  EXPECT(dfa.thrashing());

  // begin_scan clears the per-scan view (the thrash signal is per-search)
  dfa.begin_scan();
  EXPECT(!dfa.thrashing());
}

TEST(lazy_dfa_alphabet_is_compressed)
{
  const auto     st  {dynamic_storage::compile("[a-z]+", real::flags::none)};
  const lazy_dfa dfa {st.program.code, st.program.classes};
  // one class for [a-z], one for everything else: an alphabet of 2, not 256.
  EXPECT_EQ(dfa.num_classes(), std::uint16_t {2});
}

// The forward pass (kFirstMatch): its reported end must equal the Pike VM's match end, for eligible
// patterns. The two acids distinguish kFirstMatch from the naive earliest-end and from longest-match.
static std::size_t pike_end(const char       * pat,
                            const std::string& text)
{
  const auto m {real::regex(pat).search(text)};
  return m.matched() ? m.end() : real::npos;
}

static std::size_t dfa_end(const char       * pat,
                           const std::string& text)
{
  const auto st {dynamic_storage::compile(pat, real::flags::none)};
  return lazy_dfa(st.program.code, st.program.classes).forward_end(text);
}

TEST(lazy_dfa_forward_pass_matches_pike_on_the_acids)
{
  EXPECT_EQ(dfa_end("a|ab", "ab"), pike_end("a|ab", "ab"));             // 1, not the longest 2
  EXPECT_EQ(dfa_end("a|ab", "ab"), std::size_t {1});
  EXPECT_EQ(dfa_end("aabaa|b", "aabaa"), pike_end("aabaa|b", "aabaa")); // 5, not the earlier-ending 3
  EXPECT_EQ(dfa_end("aabaa|b", "aabaa"), std::size_t {5});
  EXPECT_EQ(dfa_end("aabaa|b", "zzaabaa"), std::size_t {7});
}

TEST(lazy_dfa_forward_pass_differential_vs_pike)
{
  // eligible byte/class patterns only (no assertions / klass_cp / lookarounds)
  const char* pats[] {
    "a|ab", "ab|a", "aabaa|b", "a*b", "(a|b)+c", "[a-c]+", "a(b|c)*d", "x?y?z", "a+a+",
    "(ab|a)(b|)", "a*a*b", "[0-9]+[.][0-9]+", "foo|foobar", "(a|)+b"};
  const char* texts[] {
    "", "a", "b", "ab", "ba", "aabaa", "abcabc", "xyz", "aaa", "abcd", "1.5", "foobar", "aaab", "cc"};
  std::size_t checked {0};
  for (const char* p : pats) {
    for (const char* t : texts) {
      const std::string s {t};
      EXPECT_EQ(dfa_end(p, s), pike_end(p, s));
      ++checked;
    }
  }
  EXPECT(checked >= 190U);
}

TEST(lazy_dfa_forward_pass_is_linear_under_state_explosion)
{
  // a state-exploding pattern: the forward pass is one left-to-right sweep, so its work is linear in the
  // text — never the per-position re-derivation that would be quadratic.
  const auto        st  {dynamic_storage::compile("(a|b)*a(a|b)(a|b)(a|b)(a|b)(a|b)(a|b)", real::flags::none)};
  const lazy_dfa    dfa {st.program.code, st.program.classes};
  const std::string big(20000, 'a');
  EXPECT(dfa.forward_end(big) != real::npos); // completes (linearly); the assertion is that it returns
}
