// The lazy-DFA forward pass and its cache frame, driven directly (nothing routes matching through it yet).
// These pin the machinery: the byte-class alphabet, the priority-ordered subset construction, the memoized
// transition cache (hit/miss), the bounded eviction (flush) + thrash detector, and the kFirstMatch forward
// pass itself — its reported end differential against the Pike VM, with a teeth-verified priority cut.
#include <utility>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"
#include "real/lazy_dfa.hpp"

using real::detail::dynamic_storage;
using real::detail::lazy_dfa;
using real::detail::reverse_dfa;

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
  lazy_dfa          dfa {st.program.code, st.program.classes, /*budget=*/ 4}; // tiny budget: force cache thrash
  const std::string big(20000, 'a');
  EXPECT(dfa.forward_end(big) != real::npos);                                 // completes (linearly) even while the cache flushes under thrash
}

// The reverse pass finds the start of the match ending at e (reverse-kLongest). Full spans (s,e) from the
// two DFA passes must equal the Pike VM span, for eligible patterns.
static std::pair<std::size_t, std::size_t> dfa_span(const char       * pat,
                                                    const std::string& text)
{
  const auto        st  {dynamic_storage::compile(pat, real::flags::none)};
  lazy_dfa          fwd {st.program.code, st.program.classes};
  const std::size_t e   {fwd.forward_end(text)};
  if (e == real::npos) {
    return {real::npos, real::npos};
  }
  reverse_dfa       rev {st.program.code, st.program.classes};
  const std::size_t s   {rev.reverse_start(text, e, 0)};
  return {s, e};
}

static std::pair<std::size_t, std::size_t> pike_span(const char       * pat,
                                                     const std::string& text)
{
  const auto m {real::regex(pat).search(text)};
  return m.matched() ? std::pair {m.start(), m.end()} : std::pair {real::npos, real::npos};
}

TEST(reverse_dfa_kLongest_acid)
{
  // a*b on "aaab": fwd e = 4; the reverse must give s = 0 (longest backward), not s = 3 (a reverse-first).
  EXPECT_EQ(dfa_span("a*b", "aaab"), (std::pair<std::size_t, std::size_t> {0, 4}));
  EXPECT_EQ(dfa_span("a*b", "aaab"), pike_span("a*b", "aaab"));
}

TEST(reverse_dfa_full_span_differential_vs_pike)
{
  const char* pats[] {
    "a|ab", "ab|a", "aabaa|b", "a*b", "(a|b)+c", "[a-c]+", "a(b|c)*d", "x?y?z", "a+a+",
    "(ab|a)(b|)", "a*a*b", "[0-9]+[.][0-9]+", "foo|foobar", "(a|)+b", "a*", "(a+)(a+)"};
  const char* texts[] {
    "", "a", "b", "ab", "ba", "aabaa", "abcabc", "xyz", "aaa", "abcd", "1.5", "foobar", "aaab", "cc", "aaaa"};
  std::size_t checked {0};
  for (const char* p : pats) {
    for (const char* t : texts) {
      const std::string s {t};
      EXPECT_EQ(dfa_span(p, s), pike_span(p, s));
      ++checked;
    }
  }
  EXPECT(checked >= 200U);
}

// End-to-end: the lazy-DFA ROUTING (forward end + reverse start + windowed Pike) must give the same spans
// AND groups as the pure Pike VM, on inputs long enough to cross the routing threshold. A periodic input
// has a known match structure; we check the count, a sampled span, and its groups.
TEST(lazy_dfa_routing_matches_pike_on_large_input)
{
  const real::regex rx {"([a-z]+)@([a-z]+)", real::flags::ascii}; // eligible: byte classes only
  std::string       text;
  for (int i = 0; i < 400; ++i) {
    text += "xy ab@cd zz "; // one match "ab@cd" per 11-byte period, well past the 512-byte threshold
  }
  std::size_t n {0};
  for (const auto& m : rx.find_iter(text)) {
    EXPECT_EQ(m[0], std::string_view {"ab@cd"});
    EXPECT_EQ(m[1], std::string_view {"ab"});
    EXPECT_EQ(m[2], std::string_view {"cd"});
    EXPECT_EQ(m.start(), (n * 12U) + 3U); // "xy " is 3 bytes, then the match
    ++n;
  }
  EXPECT_EQ(n, 400U);
  // a no-match large input must reject (routed) and yield nothing
  const std::string none(6000, 'z');
  EXPECT(!rx.search(none));
  // search from a resume point routes the same
  EXPECT_EQ(rx.search(text, 20, text.size()).start(), 27U); // matches at 3,15,27,...; first >= 20 is 27
}

// L2.5: klass_cp (\w \d \s in text mode) becomes DFA-eligible via a byte-program that expands each into its
// UTF-8 byte-range sub-automaton. The two DFA passes over that byte-program must give the same spans as the
// Pike VM — including non-ASCII word characters (é, α), whose multi-byte encodings the expansion recognises.
static std::pair<std::size_t, std::size_t> byte_dfa_span(const char       * pat,
                                                         const std::string& text)
{
  const auto st {dynamic_storage::compile(pat, real::flags::none)};
  const auto bp {real::detail::build_byte_program(st.program.view())};
  if (!bp.eligible) {
    return {12345U, 12345U}; // sentinel: not expected here
  }
  lazy_dfa          fwd {bp.code, bp.classes};
  const std::size_t e   {fwd.forward_end(text)};
  if (e == real::npos) {
    return {real::npos, real::npos};
  }
  reverse_dfa       rev {bp.code, bp.classes};
  const std::size_t s   {rev.reverse_start(text, e, 0)};
  return {s, e};
}

TEST(byte_program_klass_cp_differential_vs_pike)
{
  const auto st {dynamic_storage::compile("\\w+", real::flags::none)};
  EXPECT(real::detail::build_byte_program(st.program.view()).eligible); // \w is now DFA-eligible via the byte-program

  const char* pats[]  {R"(\w+)", R"(\d+)", R"((\w+)@(\w+))", R"(\w+\s\w+)", R"(\d+[.]\d+)", R"(\w*)", R"(a\w+b)", R"(\s+)"};
  const char* texts[] {
    "", "hello", "caf\xC3\xA9", "a\xC3\xA9""9_ b", "12.5", "x@y", "  ", "na\xC3\xAFve word",
    "\xCE\xB1\xCE\xB2\xCE\xB3""42"};
  std::size_t checked {0};
  for (const char* p : pats) {
    for (const char* t : texts) {
      const std::string s {t};
      EXPECT_EQ(byte_dfa_span(p, s), pike_span(p, s));
      ++checked;
    }
  }
  EXPECT(checked >= 70U);
}

// L2.5 end-to-end: a default-flags klass_cp pattern (\w) routes through the byte-program, and the windowed
// Pike still yields correct groups — including a non-ASCII word character (é) whose 2-byte encoding the
// byte-program recognises and whose bytes land inside a capture group.
TEST(lazy_dfa_routing_klass_cp_unicode_default)
{
  const real::regex rx {"(\\w+)@(\\w+)"}; // default flags: \w is klass_cp, routed via the byte-program
  std::string       text;
  for (int i = 0; i < 500; ++i) {
    text += "-- caf\xC3\xA9@world ok "; // "café@world" once per period, well past the routing threshold
  }
  std::size_t n {0};
  for (const auto& m : rx.find_iter(text)) {
    EXPECT_EQ(m[0], std::string_view {"caf\xC3\xA9@world"});
    EXPECT_EQ(m[1], std::string_view {"caf\xC3\xA9"}); // café — the 2-byte é is inside group 1
    EXPECT_EQ(m[2], std::string_view {"world"});
    ++n;
  }
  EXPECT_EQ(n, 500U);
}
