// Public API on the simplest patterns: literals, concatenation, escapes.
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/regex.hpp" // the common guess; alias of real.hpp

using namespace std::string_view_literals;

TEST(literal_match_is_anchored_prefix)
{
  const real::regex rx("hello");
  EXPECT(rx.match("hello"));
  EXPECT(rx.match("hello world"));
  EXPECT(!rx.match("say hello"));
  EXPECT_EQ(rx.match("hello world")[0], "hello"sv);
  EXPECT_EQ(rx.match("hello world").start(), 0U);
  EXPECT_EQ(rx.match("hello world").end(), 5U);
}

TEST(literal_fullmatch_consumes_everything)
{
  const real::regex rx("hello");
  EXPECT(rx.fullmatch("hello"));
  EXPECT(!rx.fullmatch("hello!"));
  EXPECT(!rx.fullmatch("hell"));
}

TEST(literal_search_finds_leftmost)
{
  const real::regex        rx("ab");
  auto                     match = rx.search("xxabyyab");
  EXPECT(match);
  EXPECT_EQ(match.start(), 2U);
  EXPECT_EQ(match.end(), 4U);
  EXPECT_EQ(match[0], "ab"sv);
  EXPECT(!rx.search("xyz"));
}

TEST(empty_pattern_matches_empty_string)
{
  const real::regex rx("");
  EXPECT(rx.match("abc"));
  EXPECT_EQ(rx.match("abc").end(), 0U);
  EXPECT(rx.fullmatch(""));
  EXPECT(!rx.fullmatch("a"));
}

TEST(escaped_metacharacters_are_literals)
{
  const real::regex rx("a\\.b");
  EXPECT(rx.match("a.b"));
  EXPECT(!rx.match("axb"));
  EXPECT(real::regex("\\\\").match("\\"));
  EXPECT(real::regex("\\*\\+\\?").match("*+?"));
}

TEST(no_match_result_is_empty)
{
  const real::regex        rx("zzz");
  auto                     match = rx.search("abc");
  EXPECT(!match);
  EXPECT_EQ(match.start(), real::npos);
  EXPECT_EQ(match.end(), real::npos);
  EXPECT_EQ(match[0], ""sv);
}

TEST(str_is_the_std_smatch_spelling_of_operator_index)
{
  const real::regex rx("(\\w+)@(\\w+)");
  const auto        match = rx.search("mail bob@host!");
  EXPECT_EQ(match.str(), match[0]);      // no argument: the whole match, like std::smatch::str()
  EXPECT_EQ(match.str(1), "bob"sv);
  EXPECT_EQ(match.str(2), "host"sv);
  EXPECT_EQ(match.str(3), ""sv);         // past the last group
  // Unset group and non-match agree with operator[] rather than throwing.
  EXPECT_EQ(real::regex("(a)?b").fullmatch("b").str(1), ""sv);
  EXPECT_EQ(rx.search("nothing here").str(), ""sv);
}

TEST(unsupported_syntax_is_rejected)
{
  EXPECT_THROWS(real::regex("(?>a|b)"), real::regex_error); // atomic groups: Tier 1 bodies only; a compound/alternating body is not
  EXPECT_THROWS(real::regex("(?P=g)"), real::regex_error);  // backrefs: v2
  EXPECT_THROWS(real::regex("\\q"), real::regex_error);
  EXPECT_THROWS(real::regex("a\\"), real::regex_error);
}

// error_kind is a stable, machine-readable classification: the C ABI exports it as REAL_ERR_*, the
// Rust binding builds Error::Unsupported from it rather than from the message text, and the Python
// binding decides whether to offer fallback=True on it. Nothing pinned WHICH construct produces
// WHICH value, which is how conditionals, callouts, recursion and subroutine calls came to be
// reported as `syntax` -- a typo, to a reader -- while program.hpp's own documentation named that
// family `unsupported`. One row per construct, both directions, and the count is asserted so a row
// deleted rather than fixed cannot pass unnoticed.
TEST(error_kind_classifies_excluded_constructs_as_unsupported)
{
  struct row
  {
    const char*      pattern;
    real::error_kind kind;
  };

  // Well formed, and beyond a linear engine: every one of these is named on the divergences page as
  // excluded BY DESIGN, so a binding must be able to tell them from a malformed pattern.
  constexpr row rows[] {
    {.pattern = "(a)\\1", .kind = real::error_kind::unsupported},                // backreference, by number
    {.pattern = "(?P<n>a)(?P=n)", .kind = real::error_kind::unsupported},        // backreference, by name
    {.pattern = "(a)(?(1)b|c)", .kind = real::error_kind::unsupported},          // conditional group
    {.pattern = "a(?C1)b", .kind = real::error_kind::unsupported},               // callout, numbered
    {.pattern = "a(?C)b", .kind = real::error_kind::unsupported},                // callout, bare
    {.pattern = "a(?R)b", .kind = real::error_kind::unsupported},                // recursion, whole pattern
    {.pattern = "(a)(?1)", .kind = real::error_kind::unsupported},               // recursion, absolute
    {.pattern = "(a)(?-1)", .kind = real::error_kind::unsupported},              // recursion, relative
    {.pattern = "(?P<n>a)(?&n)", .kind = real::error_kind::unsupported},         // subroutine call, PCRE spelling
    {.pattern = "(?P<n>a)(?P>n)", .kind = real::error_kind::unsupported},        // subroutine call, Python spelling
    {.pattern = "(?=(?=a))", .kind = real::error_kind::unsupported},             // nested lookaround
    {.pattern = "(?<=a*)b", .kind = real::error_kind::unsupported},              // unbounded lookaround
    // Rejected "not supported YET" rather than by design -- but still well formed and still beyond
    // this engine, so the classification is the same one, and it is what lets a binding offer the
    // remedy that demonstrably works: re compiles all four and the Python fallback runs them.
    {.pattern = "(?:ab)*+", .kind = real::error_kind::unsupported},   // possessive, compound body
    {.pattern = "(?:ab)++", .kind = real::error_kind::unsupported},
    {.pattern = "(?>ab|a)", .kind = real::error_kind::unsupported},   // atomic group, alternating body
    // A possessive inside a lookaround must be BOUNDED to reach that refusal at all: an
    // unbounded one is stopped earlier, by the unbounded-lookaround check. `(?=a*+)b` was the
    // wrong witness -- it never reached the site it was meant to pin, and a sabotage proved it.
    {.pattern = "(?=a{1,3}+)b", .kind = real::error_kind::unsupported},
    {.pattern = "(?<=a{1,2}+)b", .kind = real::error_kind::unsupported},
    // Malformed: `syntax`, and a binding must NOT offer a delegating remedy for these.
    {.pattern = "(unclosed", .kind = real::error_kind::syntax},
    {.pattern = "a)", .kind = real::error_kind::syntax},
    {.pattern = "(?Z)", .kind = real::error_kind::syntax},                       // an extension that is simply unknown
    {.pattern = "(?", .kind = real::error_kind::syntax},
    {.pattern = "a{3,1}", .kind = real::error_kind::syntax},
  };

  std::size_t checked {0};
  for (const row& r : rows) {
    try {
      const real::regex rx(r.pattern);
      EXPECT(false);  // every row above must be rejected
    }
    catch (const real::regex_error& ex) {
      EXPECT_EQ(static_cast<int>(ex.kind()), static_cast<int>(r.kind));
      ++checked;
    }
  }
  EXPECT_EQ(checked, sizeof(rows) / sizeof(rows[0]));
  EXPECT_EQ(checked, 22U);  // the denominator, so a deleted row fails rather than shrinking silently
}

TEST(excluded_constructs_are_named_not_called_unknown)
{
  // "unknown extension" reads as a typo and tells the reader nothing. The constructs excluded by
  // design must name themselves; what is genuinely unrecognised must still say so.
  const auto cause = [](const char* pattern) {
                       try {
                         const real::regex rx(pattern);
                       }
                       catch (const real::regex_error& ex) {
                         return std::string(ex.what());
                       }
                       return std::string {};
                     };

  EXPECT(cause("a(?C1)b").find("callouts are not supported") != std::string::npos);
  EXPECT(cause("a(?R)b").find("pattern recursion is not supported") != std::string::npos);
  EXPECT(cause("(a)(?1)").find("pattern recursion is not supported") != std::string::npos);
  EXPECT(cause("(?P<n>a)(?&n)").find("subroutine calls are not supported") != std::string::npos);
  EXPECT(cause("(?P<n>a)(?P>n)").find("subroutine calls are not supported") != std::string::npos);
  EXPECT(cause("(a)(?(1)b|c)").find("conditional groups are not supported") != std::string::npos);
  EXPECT(cause("(?Z)").find("unknown extension") != std::string::npos);
}

// The wording and the POSITION are re's, and both were wrong: "unknown extension at position 2"
// said where without saying what, and pointed at the character that ended the construct rather
// than at the `?` that opened it. This is the third report in the same family from the same
// reader, so the whole surface is pinned here rather than the two cases sent.
TEST(unknown_extension_names_the_construct)
{
  const auto cause = [](const char* pattern) {
                       try {
                         const real::regex rx(pattern);
                       }
                       catch (const real::regex_error& ex) {
                         return std::string(ex.what());
                       }
                       return std::string {};
                     };

  // `regex_error at N: ` prefixes what(), so N is the reported position.
  EXPECT(cause("(?z)a") == "regex_error at 1: unknown extension ?z");
  EXPECT(cause("(?)a") == "regex_error at 1: unknown extension ?)");
  EXPECT(cause("(?P)a") == "regex_error at 1: unknown extension ?P)");   // consumed P, then `)`
  EXPECT(cause("(?P:a)") == "regex_error at 1: unknown extension ?P:");
  EXPECT(cause("(?\\)a") == "regex_error at 1: unknown extension ?\\)"); // an escape is two chars
  EXPECT(cause("(?\\:a)") == "regex_error at 1: unknown extension ?\\:");
  // Nested: the `?` reported is the inner one, not the pattern's first.
  EXPECT(cause("a(b(?q)c)") == "regex_error at 4: unknown extension ?q");
  // End of pattern has no character to name, and re says so at the read offset, not at the `?`.
  EXPECT(cause("(?") == "regex_error at 2: unexpected end of pattern");
  EXPECT(cause("(?P") == "regex_error at 3: unexpected end of pattern");
}

// A truncated escape used to state the RULE (`invalid \x escape: expected two hex digits`) where
// re states the READING: `incomplete escape \x1 at position 1`, quoting the characters consumed and
// pointing at the backslash rather than at the cursor. Same defect as `unknown extension`, one
// surface lower, and the third message family an outside audit reported.
TEST(incomplete_escape_quotes_what_it_read)
{
  const auto cause = [](const char* pattern) {
                       try {
                         const real::regex rx(pattern);
                       }
                       catch (const real::regex_error& ex) {
                         return std::string(ex.what());
                       }
                       return std::string {};
                     };

  EXPECT(cause("a\\x") == "regex_error at 1: incomplete escape \\x");
  EXPECT(cause("a\\x1") == "regex_error at 1: incomplete escape \\x1");
  EXPECT(cause("a\\u123") == "regex_error at 1: incomplete escape \\u123");
  EXPECT(cause("a\\U1234567") == "regex_error at 1: incomplete escape \\U1234567");
  // Inside a class the class-escape parser has its own backslash, and reports at ITS offset.
  EXPECT(cause("[a\\x]") == "regex_error at 2: incomplete escape \\x");
  EXPECT(cause("[a\\u12]") == "regex_error at 2: incomplete escape \\u12");
  // A COMPLETE escape whose value is out of range is a different fault and keeps its own message:
  // re calls it `bad escape`, which says less. Naming the cause is the same trade as
  // `callouts are not supported` against re's `unknown extension`.
  EXPECT(cause("a\\U00110000").find("code point out of range") != std::string::npos);
  // And a complete, in-range one still compiles.
  EXPECT(cause("a\\xAB").empty());
  EXPECT(cause("a\\U0010FFFF").empty());
}

// `bad character range at position 1` said WHERE on the one diagnostic where the WHAT matters most:
// a range is two endpoints and an order, and the message named none of them. re reports
// `bad character range z-a`. The parser held both ends the whole time -- the caller rewinds the read
// offset to the range's start before failing, so the start was already the reported position and the
// end was one capture away. Same family as `unknown extension` and `incomplete escape`.
TEST(bad_character_range_quotes_the_range)
{
  const auto cause = [](const char* pattern) {
                       try {
                         const real::regex rx(pattern);
                       }
                       catch (const real::regex_error& ex) {
                         return std::string(ex.what());
                       }
                       return std::string {};
                     };

  EXPECT(cause("[z-a]") == "regex_error at 1: bad character range z-a");
  EXPECT(cause("[a-Z]") == "regex_error at 1: bad character range a-Z");
  EXPECT(cause("[9-0]") == "regex_error at 1: bad character range 9-0");
  // A shorthand class cannot be an ENDPOINT, on either side, and both halves quote what they read.
  EXPECT(cause("[\\d-z]") == "regex_error at 1: bad character range \\d-z");
  EXPECT(cause("[z-\\d]") == "regex_error at 1: bad character range z-\\d");
  // Punctuation endpoints: a literal `-` as the start, and a `]` that is a member because it is
  // first. Neither needs escaping to be quoted back.
  EXPECT(cause("[--,]") == "regex_error at 1: bad character range --,");
  EXPECT(cause("[]-!]") == "regex_error at 1: bad character range ]-!");
  // The range's own offset, not the pattern's first class.
  EXPECT(cause("xy[b-a]") == "regex_error at 3: bad character range b-a");
  EXPECT(cause("[abc][z-a]") == "regex_error at 6: bad character range z-a");
  // A DIVERGENCE from re, stated: re prints `\x-\x` here, truncating each endpoint at the escape's
  // letter because that is where its tokenizer stopped. Quoting the source is more use to whoever
  // wrote it, and the difference only concerns how much of an already-rejected range is echoed.
  EXPECT(cause("[\\x7f-\\x20]") == "regex_error at 1: bad character range \\x7f-\\x20");
  // Well-ordered ranges still compile, including the ones whose endpoints are escapes.
  EXPECT(cause("[a-z]").empty());
  EXPECT(cause("[0-9]").empty());
  EXPECT(cause("[\\x20-\\x7f]").empty());
  // And a trailing `-` before `]` is a literal member, not an unterminated range.
  EXPECT(cause("[a-]").empty());
}

// `unsupported escape sequence` named the category and not the escape, so `\q` and `\y` on one line
// gave one indistinguishable message -- and it pointed at the escaped character rather than at the
// `\`, where re reports and where `incomplete escape` already reported. The kind stays
// `unsupported`, so what a binding branches on does not move; only what a reader is told.
TEST(unsupported_escape_names_the_escape)
{
  const auto cause = [](const char* pattern) {
                       try {
                         const real::regex rx(pattern);
                       }
                       catch (const real::regex_error& ex) {
                         return std::string(ex.what());
                       }
                       return std::string {};
                     };

  EXPECT(cause("\\q") == "regex_error at 0: unsupported escape sequence \\q");
  EXPECT(cause("ab\\q") == "regex_error at 2: unsupported escape sequence \\q");
  // Two unrecognised escapes in one pattern: the FIRST is reported, and it is now possible to tell
  // which one that was.
  EXPECT(cause("a\\q\\y") == "regex_error at 1: unsupported escape sequence \\q");
  EXPECT(cause("a\\y\\q") == "regex_error at 1: unsupported escape sequence \\y");
  // Inside a class the class-escape parser has its own backslash and reports at ITS offset.
  EXPECT(cause("[a\\q]") == "regex_error at 2: unsupported escape sequence \\q");
  // Escapes REAL does implement still compile, so the message is about recognition and not about
  // every backslash.
  EXPECT(cause("\\d\\w\\s\\b").empty());
  EXPECT(cause("\\x41").empty());
  EXPECT(cause("a\\-b").empty()); // escaped punctuation keeps its literal meaning
}

// Three diagnostics pointed where the SCAN stopped instead of where the fault begins. `re` points
// at the construct: the `(` of a misplaced flag group, the second quantifier of a double repeat,
// the first count of an inverted range. On a long pattern the position is how the fault is found at
// all, so three characters of drift is the wrong place — same family as the escapes f931823 moved
// to the backslash.
TEST(error_cursor_points_at_the_construct_not_past_it)
{
  const auto at = [](const char* pattern) {
                    try {
                      const real::regex rx(pattern);
                    }
                    catch (const real::regex_error& ex) {
                      return static_cast<long>(ex.position());
                    }
                    return -1L;
                  };

  // A misplaced flag group: the `(`, not the byte after the flag letters. The drift grew with the
  // flag run, so `(?imsx)` was further off than `(?i)`.
  EXPECT(at("a(?i)b") == 1);
  EXPECT(at("ab(?i)c") == 2);
  EXPECT(at("a(?im)b") == 1);
  EXPECT(at("a(?imsx)b") == 1);

  // A double quantifier: the SECOND one. `*` and `?` consume nothing and already reported here;
  // `{n}` consumes, and reported past its own `}`.
  EXPECT(at("a{2}{3}") == 4);
  EXPECT(at("ab{1}{2}") == 5);
  EXPECT(at("a**") == 2);      // unchanged: this half was always right
  EXPECT(at("a{2}{3}{4}") == 4);

  // An inverted repeat: the first character INSIDE the braces, where the counts are, not the brace
  // that introduces them.
  EXPECT(at("a{2,1}") == 2);
  EXPECT(at("ab{5,2}") == 3);
  EXPECT(at("a{2,1}b") == 2);
  EXPECT(at("a{10,2}") == 2);  // a multi-digit min still reports its FIRST digit

  // Patterns that must still compile, so the rewinds cannot be reached by a well-formed quantifier
  // or a leading flag group.
  EXPECT(at("(?i)a") == -1);
  EXPECT(at("a{2,3}") == -1);
  EXPECT(at("a{2}") == -1);
}

// `bad character in group name` named the rule and neither the name nor which of four distinct
// faults occurred. re splits them, quotes the name it read, and reports at the name's first byte.
// Second of the three message families from the outside audit.
TEST(group_name_diagnostics_quote_the_name)
{
  const auto cause = [](const char* pattern) {
                       try {
                         const real::regex rx(pattern);
                       }
                       catch (const real::regex_error& ex) {
                         return std::string(ex.what());
                       }
                       return std::string {};
                     };

  EXPECT(cause("(?P<>a)") == "regex_error at 4: missing group name");
  EXPECT(cause("(?P<1x>a)") == "regex_error at 4: bad character in group name '1x'");
  EXPECT(cause("(?P<a b>c)") == "regex_error at 4: bad character in group name 'a b'");
  EXPECT(cause("(?P<x") == "regex_error at 4: missing >, unterminated name");
  EXPECT(cause("(?P<x>a)(?P<x>b)")
         == "regex_error at 12: redefinition of group name 'x' as group 2; was group 1");
  // The name is quoted whole, so a multi-byte code point survives it.
  EXPECT(cause("(?P<²>a)") == "regex_error at 4: bad character in group name '²'");
  // And a valid name still compiles, ASCII or not.
  EXPECT(cause("(?P<x>a)").empty());
  EXPECT(cause("(?P<é>a)").empty());
}

TEST(regex_error_reports_position)
{
  try {
    real::regex rx("ab\\q");
    EXPECT(false);
  }
  catch (const real::regex_error& ex) {
    // The BACKSLASH, not the escaped character. `re` reports `bad escape \q at position 2` here,
    // and fail_incomplete_escape already pointed at the `\` for a truncated escape -- pointing at
    // the `q` for an unrecognised one made the same family answer two different questions.
    EXPECT_EQ(ex.position(), 2U);
    // And the escape is NAMED: `\q` and `\y` on one line used to give one indistinguishable
    // message, so the quote is what makes the position usable rather than decorative.
    EXPECT(std::string_view(ex.what()).find("\\q") != std::string_view::npos);
  }
}

TEST(pattern_and_group_count_accessors)
{
  const real::regex rx("abc");
  EXPECT_EQ(rx.pattern(), "abc"sv);
  EXPECT_EQ(rx.group_count(), 0U);
}

TEST(matching_works_on_embedded_nul_and_binary_text)
{
  const real::regex rx("b");
  EXPECT_EQ(rx.search("a\0b"sv).start(), 2U);
}

TEST(dynamic_large_slots_sbo)
{
  // Exercise small_vec heap path for >32 slots (evidence that SBO helps common small cases
  // while correctly growing for rare large-group patterns). Built from old container inspection.
  std::string pat;
  for (int i = 0; i < 40; ++i) {
    pat += "(x)";
  }
  real::regex rx(pat);
  std::string subject(40, 'x');
  auto        match = rx.search(subject); // owning string -> use view internally; avoids deleted && overload
  EXPECT(match.matched());
  EXPECT(match.size() > 32);              // >32 groups -> slot count >64, forces reserve in small_vec (heap path)
  EXPECT(match[39] == "x");               // last group participates

  // Explicit copy after growth: exercises small_vec heap copy ctor (was a coverage gap
  // for the SBO advancement; secures that grown results can be copied without issue).
  auto m2 = match; // NOLINT(performance-unnecessary-copy-initialization) — the copy is the test
  EXPECT(m2.size() > 32);
  EXPECT(m2[39] == "x");
}

TEST(dynamic_find_all_exercises_result_copies_with_sbo)
{
  // Bonify test for small_vec in dynamic slot_storage: find_all creates
  // std::vector<result_type> and push_back copies the small_vec slots.
  // Exercises inline SBO copy path for common small-group case.
  real::regex rx("(\\w+)");
  auto        results = rx.find_all("a1 b22 c333");
  EXPECT_EQ(results.size(), 3U);
  EXPECT_EQ(results[0][1], "a1");
  EXPECT_EQ(results[1][1], "b22");
  EXPECT_EQ(results[2][1], "c333");
}

TEST(small_vec_grown_result_copy_exercises_heap_copy)
{
  // Bonify coverage for small_vec: explicit copy of a grown (>32 slots)
  // result exercises heap copy ctor/assign (missed region in prior coverage).
  // Relevant for robustness of SBO in dynamic results with many groups.
  std::string pat;
  for (int i = 0; i < 40; ++i) {
    pat += "(x)";
  }
  real::regex rx(pat);
  std::string subject(40, 'x');
  auto        match  = rx.search(subject);
  auto        m2     = match; // NOLINT(performance-unnecessary-copy-initialization) — copy after growth is the test
  EXPECT(m2.size() > 32);
  EXPECT(m2[39] == "x");

  // Bonus: move after growth to exercise small_vec move ctor/assign for heap case.
  auto m3 = std::move(m2);
  EXPECT(m3.size() > 32);
  EXPECT(m3[39] == "x");
}

TEST(regex_copy_assignment_resets_and_rebuilds_the_immutables_cache)
{
  // basic_regex has no custom copy-assignment operator; the compiler-generated one member-wise assigns
  // dynamic_storage, which in turn assigns detail::regex_immutables (its mutable lazy-DFA/one-pass cache)
  // via regex_immutables::operator=(const&) -- invalidates built_for (each regex keeps its OWN, independent,
  // lazily-rebuilt cache rather than inheriting the source's already-built one). Warm both regexes' caches
  // first (a routed search builds immutables), THEN copy-assign, and confirm the destination
  // still matches correctly afterward -- the invalidate must not corrupt anything the rebuild depends on.
  real::regex a {R"((\w+)@(\w+))"};
  real::regex b {R"((\d+)-(\d+))"};
  std::string long_text;
  for (int i = 0; i < 50; ++i) {
    long_text += "aa@bb 12-34 "; // past the routing threshold, warms each regex's own immutables cache
  }
  (void) a.search(long_text);
  (void) b.search(long_text);

  a = b; // copy-assignment (not construction): exercises regex_immutables::operator=(const&)

  const auto m {a.search("99-88")};
  EXPECT(m.matched());
  EXPECT_EQ(m[1], "99");
  EXPECT_EQ(m[2], "88");
}

// --- Program size limit (config.hpp + compiler guard, #1+#3) ---------------
// Prevents the validated DoS: 24-char nested bounded quant pattern expanding
// via unroll to ~GB allocation / millions of NFA instrs. We cap at 256Ki
// (allows practical a{1000} etc, rejects the blowup cases).
// The error is raised during emission, so peak mem stays bounded (~few MiB).

TEST(program_size_limit)
{
  // Reasonable large bounded (a{200} emits ~203 instr) must still work.
  // Well below cap; exercises unroll path without hitting limit.
  {
    real::regex r("a{200}");
    EXPECT(r.raw_program().code.size() > 150);
    EXPECT(r.raw_program().code.size() < 300);
    // matching still functions
    std::string subject(200, 'a');
    auto        match = r.search(subject);
    EXPECT(match.matched());
    EXPECT_EQ(match[0].size(), 200U);
  }

  // A nested shape that multiplies unrolls beyond cap must raise cleanly.
  // Tune exponents so product of unrolls > 262144 while pattern text small.
  // Rough: 400 * 400 * 2  > cap.
  {
    std::string pat   = "((a{400}){400}){2}";
    bool        threw = false;
    std::string what;
    try {
      real::regex r(pat);
    } catch (const real::regex_error& ex) {
      threw = true;
      what  = ex.what();
    } catch (...) {
      // other exception bad
    }
    EXPECT(threw);
    // Message contains "program too large" (position 0 as we don't track emit site precisely)
    EXPECT(what.find("program too large") != std::string::npos);
  }
}

// match_result::spans() -- the flat [start0,end0,start1,end1,...] view of the capture slots. It is the
// raw storage (the C ABI's fill reads straight across it after checking the return code), so the two
// facts worth pinning are the layout/length contract and the empty case.
TEST(match_result_spans_flat_layout)
{
  const real::regex re {R"((\w+)@(\w+))"};
  const auto        m  {re.search("mail root@localhost end")};
  EXPECT(m.matched());

  const auto flat {m.spans()};
  EXPECT_EQ(flat.size(), 2U * m.size()); // two slots per group, group 0 included
  EXPECT_EQ(flat.size(), 6U);
  for (std::size_t g = 0; g < m.size(); ++g) {
    EXPECT_EQ(flat[2 * g], m.start(g));         // identical to the per-group accessors on a match
    EXPECT_EQ(flat[(2 * g) + 1], m.end(g));
  }
  EXPECT_EQ(flat[0], 5U);
  EXPECT_EQ(flat[1], 19U);

  // A result with no slots at all (default-constructed) yields an empty view rather than a
  // one-past-the-end pointer.
  const real::match_result empty;
  EXPECT(empty.spans().empty());
  EXPECT_EQ(empty.spans().size(), 0U);
}
