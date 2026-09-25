//! The C ABI shim (bindings/c) exercised from the instrumented test binary, so real_capi.cpp is covered by
//! the coverage floor and run under ASan/UBSan in the sanitize build — the raw-pointer surface the Rust and
//! future bindings sit on. The pure-C-linkage smoke stays in bindings/c/test_capi.c.
#include <sciforge/test/framework.hpp>

#include <real_capi.h>

#include <real/real.hpp>
#include <real/version.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {
  real_regex* compile(const char  * pat,
                      std::uint32_t flags,
                      int&          code)
  {
    char err[256] = {0};
    return real_compile(pat, std::strlen(pat), flags, err, sizeof err, &code);
  }
}

// The interface reports the version it was built with, so a binding that links it can refuse a mismatch.
TEST(capi_reports_its_abi_and_release_versions)
{
  EXPECT_EQ(real_abi_version(), static_cast<uint32_t>(REAL_ABI_VERSION));
  EXPECT_EQ(std::string_view(real_version()), std::string_view(REAL_VERSION_STRING));
}

TEST(capi_compile_iterate_groups)
{
  int         code = 99;
  real_regex* re   = compile("(\\w+)@(\\w+)", 0, code);
  EXPECT(re != nullptr);
  EXPECT(code == REAL_ERR_NONE);
  EXPECT(real_group_count(re) == 3);

  const char* text = "a@b and cd@ef";
  real_iter * it   = real_find_iter(re, text, std::strlen(text));
  EXPECT(it != nullptr);
  std::vector<std::size_t> spans(6);
  EXPECT(real_iter_next(it, spans.data()) == 1);
  EXPECT(spans[0] == 0 && spans[1] == 3 && spans[2] == 0 && spans[3] == 1 && spans[4] == 2 && spans[5] == 3);
  EXPECT(real_iter_next(it, spans.data()) == 1);
  EXPECT(real_iter_next(it, spans.data()) == 0);
  real_iter_free(it);
  real_free(re);
}

TEST(capi_group_names_and_find_at)
{
  int         code = 0;
  real_regex* re   = compile("(?P<user>\\w+)@(?P<host>\\w+)", 0, code);
  EXPECT(re != nullptr);
  char name[64];
  EXPECT(real_group_name(re, 0, name, sizeof name) == 0);                            // group 0 unnamed
  EXPECT(real_group_name(re, 1, name, sizeof name) == 4 && std::strcmp(name, "user") == 0);
  EXPECT(real_group_name(re, 2, name, sizeof name) == 4 && std::strcmp(name, "host") == 0);
  EXPECT(real_group_name(re, 9, name, sizeof name) == 0);                            // out of range
  EXPECT(real_group_name(re, 1, nullptr, 0) == 4);                                   // length query, no buffer

  const char             * text = "a@b cd@ef";
  real_iter              * at   = real_find_iter_at(re, text, std::strlen(text), 4); // from offset 4 -> cd@ef only
  std::vector<std::size_t> spans(6);
  EXPECT(real_iter_next(at, spans.data()) == 1 && spans[0] == 4 && spans[1] == 9);
  EXPECT(real_iter_next(at, spans.data()) == 0);
  real_iter_free(at);
  real_free(re);
}

TEST(capi_error_codes_and_null_iter)
{
  int code = 0;
  EXPECT(compile("(", 0, code) == nullptr);
  EXPECT(code == REAL_ERR_SYNTAX);                 // malformed
  EXPECT(compile("(?P=g)", 0, code) == nullptr);
  EXPECT(code == REAL_ERR_UNSUPPORTED);            // well-formed but unsupported (named backreference)
  EXPECT(compile("(\\w+)\\1", 0, code) == nullptr);
  EXPECT(code == REAL_ERR_UNSUPPORTED);            // backreference
  // a null iterator is reported, never dereferenced
  std::vector<std::size_t> spans(2);
  EXPECT(real_iter_next(nullptr, spans.data()) == -1);
}

namespace {
  // The engine's own verdict on a pattern, read through the C++ API: the oracle the _ex entry points
  // must hand over unchanged.
  real::regex_error engine_error(std::string_view pat)
  {
    try {
      real::regex rx {pat};
    }
    catch (const real::regex_error& e) {
      return e;
    }
    return real::regex_error {"compiled", static_cast<std::size_t>(-1)};
  }
}

TEST(capi_compile_ex_hands_the_position_over)
{
  constexpr std::size_t no_pos = static_cast<std::size_t>(-1);
  // A late error, so a position of 0 cannot pass by accident; a syntax and an unsupported case.
  for (const char* pat : {"abc(def", "ab{3,1}", "xy(\\w)\\1", "a)"}) {
    const real::regex_error want = engine_error(pat);
    EXPECT(want.position() != no_pos);
    char        err[256] = {0};
    int         code     = 99;
    std::size_t pos      = 12345;
    EXPECT(real_compile_ex(pat, std::strlen(pat), 0, err, sizeof err, &code, &pos) == nullptr);
    EXPECT(pos == want.position());
    EXPECT(std::string(err) == want.cause());   // the bare cause, no `regex_error at N: ` prefix
    EXPECT(code == (want.kind() == real::error_kind::unsupported ? REAL_ERR_UNSUPPORTED : REAL_ERR_SYNTAX));

    // The plain entry point keeps the formatted message, which carries the same position in its text.
    char plain[256] = {0};
    EXPECT(real_compile(pat, std::strlen(pat), 0, plain, sizeof plain, &code) == nullptr);
    EXPECT(std::string(plain) == want.what());
  }
  EXPECT(engine_error("xy(\\w)\\1").kind() == real::error_kind::unsupported);
  EXPECT(engine_error("abc(def").position() > 0);

  // Success, and a failure with no pattern offset, both write the sentinel.
  char        err[256] = {0};
  int         code     = 99;
  std::size_t pos      = 12345;
  real_regex* ok       = real_compile_ex("a+", 2, 0, err, sizeof err, &code, &pos);
  EXPECT(ok != nullptr && code == REAL_ERR_NONE && pos == no_pos);
  real_free(ok);
  pos = 12345;
  EXPECT(real_compile_ex(nullptr, 3, 0, err, sizeof err, &code, &pos) == nullptr);
  EXPECT(pos == no_pos && code == REAL_ERR_SYNTAX && std::strlen(err) > 0);
  // A NULL err_pos is tolerated, like a NULL code.
  EXPECT(real_compile_ex("(", 1, 0, nullptr, 0, nullptr, nullptr) == nullptr);
}

TEST(capi_set_compile_ex_hands_the_position_over)
{
  constexpr std::size_t   no_pos   = static_cast<std::size_t>(-1);
  const char* const       pats[3]  = {"a", "bc(d", "e"};
  const std::size_t       lens[3]  = {1, 4, 1};
  const real::regex_error member   = engine_error(pats[1]);
  char                    err[256] = {0};
  int                     code     = 99;
  std::size_t             pos      = 12345;
  EXPECT(real_set_compile_ex(pats, lens, 3, 0, err, sizeof err, &code, &pos) == nullptr);
  EXPECT(pos == member.position());                                          // an offset inside the failing member
  EXPECT(code == REAL_ERR_SYNTAX);
  EXPECT(std::string(err).find("(in pattern 1 of 3)") != std::string::npos); // which member: in the cause
  EXPECT(std::string(err).rfind("regex_error at", 0) != 0);                  // and no position prefix

  char plain[256] = {0};
  EXPECT(real_set_compile(pats, lens, 3, 0, plain, sizeof plain, &code) == nullptr);
  EXPECT(std::string(plain).rfind("regex_error at " + std::to_string(member.position()) + ": ", 0) == 0);

  // A null array and a null member have no pattern offset.
  pos = 12345;
  EXPECT(real_set_compile_ex(nullptr, nullptr, 2, 0, err, sizeof err, &code, &pos) == nullptr);
  EXPECT(pos == no_pos && code == REAL_ERR_SYNTAX);
  const char* const     null_member[1] = {nullptr};
  const std::size_t     null_len[1]    = {5};
  pos = 12345;
  EXPECT(real_set_compile_ex(null_member, null_len, 1, 0, err, sizeof err, &code, &pos) == nullptr);
  EXPECT(pos == no_pos && std::string(err) == "null pattern at index 0");

  pos = 12345;
  real_regex_set* ok = real_set_compile_ex(pats, lens, 1, 0, err, sizeof err, &code, &pos);
  EXPECT(ok != nullptr && code == REAL_ERR_NONE && pos == no_pos);
  real_set_free(ok);
}

TEST(capi_defensive_paths)
{
  // Null errbuf and null code are tolerated (the guarded branches in real_compile / write_err).
  EXPECT(real_compile("(", 1, 0, nullptr, 0, nullptr) == nullptr);

  // errbuf truncation: a tiny buffer receives a NUL-terminated prefix, never an overrun.
  char tiny[4] = {'x', 'x', 'x', 'x'};
  int  code    = 0;
  EXPECT(real_compile("(", 1, 0, tiny, sizeof tiny, &code) == nullptr);
  EXPECT(tiny[3] == '\0' && std::strlen(tiny) <= 3);
  EXPECT(code == REAL_ERR_SYNTAX);

  // real_group_name reports the full length but truncates into a small buffer, terminated.
  real_regex* named = compile("(?P<longname>\\w+)", 0, code);
  EXPECT(named != nullptr);
  char nb[4];
  EXPECT(real_group_name(named, 1, nb, sizeof nb) == 8); // "longname" is 8 chars
  EXPECT(std::strlen(nb) == 3 && nb[3] == '\0');         // truncated prefix, terminated
  real_free(named);
}

TEST(capi_null_handle_contracts)
{
  // Enumerated null-re surface: every function that takes a real_regex* must not crash and must
  // honour the documented sentinel (aligned with real_match / real_sub).
  EXPECT(real_group_count(nullptr) == 0);

  char name[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
  EXPECT(real_group_name(nullptr, 1, name, sizeof name) == 0);
  EXPECT(name[0] == '\0');

  EXPECT(real_find_iter(nullptr, "ab", 2) == nullptr);
  EXPECT(real_find_iter_at(nullptr, "ab", 2, 0) == nullptr);
  EXPECT(real_find_iter_between(nullptr, "ab", 2, 0, 2) == nullptr);

  real_free(nullptr); // intentional no-op
}

namespace {
  constexpr std::size_t capi_error = static_cast<std::size_t>(-1);

  // real_sub through its two-call convention: size, then fill, and both calls must agree. A refusal
  // returns "<error: message>" so a case reads as one comparison.
  std::string sub(const real_regex* re,
                  std::string_view  text,
                  std::string_view  repl,
                  std::size_t       count,
                  std::size_t     * n_subs = nullptr)
  {
    char              err[128] = {0};
    const std::size_t need     = real_sub(re, text.data(), text.size(), repl.data(), repl.size(), count,
                                          nullptr, 0, n_subs, err, sizeof err);
    if (need == capi_error) {
      return std::string("<error: ") + err + ">";
    }
    std::string out(need, '?');
    EXPECT(real_sub(re, text.data(), text.size(), repl.data(), repl.size(), count,
                    out.data(), out.size(), n_subs, err, sizeof err) == need);
    return out;
  }

  std::string expand(const real_regex * re,
                     std::string_view   text,
                     const std::size_t* spans,
                     std::size_t        nspans,
                     std::string_view   repl)
  {
    char              err[128] = {0};
    const std::size_t need     = real_expand(re, text.data(), text.size(), spans, nspans, repl.data(), repl.size(),
                                             nullptr, 0, err, sizeof err);
    if (need == capi_error) {
      return std::string("<error: ") + err + ">";
    }
    std::string out(need, '?');
    EXPECT(real_expand(re, text.data(), text.size(), spans, nspans, repl.data(), repl.size(),
                       out.data(), out.size(), err, sizeof err) == need);
    return out;
  }
}

TEST(capi_find_iter_between_bounds_both_sides)
{
  int         code = 0;
  real_regex* re   = compile("\\w+", 0, code);
  const char* text = "aa bb aaa cc aa";
  real_iter*  it   = real_find_iter_between(re, text, 15, 3, 9);  // the region "bb aaa"
  std::size_t spans[2];
  EXPECT(real_iter_next(it, spans) == 1 && spans[0] == 3 && spans[1] == 5);
  EXPECT(real_iter_next(it, spans) == 1 && spans[0] == 6 && spans[1] == 9);
  EXPECT(real_iter_next(it, spans) == 0);                        // "cc" and "aa" lie past the end
  real_iter_free(it);
  real_iter* empty = real_find_iter_between(re, text, 15, 9, 3); // start > end: exhausted, not an error
  EXPECT(empty != nullptr && real_iter_next(empty, spans) == 0);
  real_iter_free(empty);
  EXPECT(real_find_iter_between(re, nullptr, 4, 0, 4) == nullptr);
  real_free(re);
}

TEST(capi_count_and_match)
{
  int         code = 0;
  real_regex* re   = compile("a(b)c", 0, code);
  EXPECT(real_count_matches(re, "abc abc x", 9) == 2);
  EXPECT(real_count_matches(re, nullptr, 0) == 0);           // (NULL, 0) is an empty subject
  EXPECT(real_count_matches(re, nullptr, 3) == capi_error);  // a claimed length with nothing behind it
  EXPECT(real_count_matches(nullptr, "abc", 3) == capi_error);

  std::size_t spans[4] = {0, 0, 0, 0};
  EXPECT(real_match(re, "xxabcyy", 7, 0, 7, REAL_MODE_SEARCH, spans) == 1);
  EXPECT(spans[0] == 2 && spans[1] == 5 && spans[2] == 3 && spans[3] == 4);
  EXPECT(real_match(re, "abcxyz", 6, 0, 6, REAL_MODE_MATCH, spans) == 1 && spans[1] == 3);
  EXPECT(real_match(re, "xabc", 4, 0, 4, REAL_MODE_MATCH, spans) == 0);        // not anchored at 0
  EXPECT(real_match(re, "abc", 3, 0, 3, REAL_MODE_FULLMATCH, spans) == 1);
  EXPECT(real_match(re, "abcx", 4, 0, 4, REAL_MODE_FULLMATCH, spans) == 0);    // a trailing byte
  EXPECT(real_match(re, "xabcx", 5, 1, 4, REAL_MODE_FULLMATCH, spans) == 1);   // the region [1, 4)
  EXPECT(spans[0] == 1 && spans[1] == 4);
  EXPECT(real_match(re, "xxabc", 5, 0, 5, 99, spans) == 1 && spans[0] == 2);   // an unknown mode searches
  EXPECT(real_match(re, "abc", 3, 0, 3, REAL_MODE_SEARCH, nullptr) == 1);      // spans are optional
  EXPECT(real_match(nullptr, "abc", 3, 0, 3, REAL_MODE_SEARCH, spans) == -1);
  EXPECT(real_match(re, nullptr, 3, 0, 3, REAL_MODE_SEARCH, spans) == -1);
  real_free(re);
}

TEST(capi_sub_template_grammar)
{
  int         code = 0;
  real_regex* re   = compile("(?P<user>\\w+)@(\\w+)", 0, code);
  std::size_t n    = 99;
  EXPECT(sub(re, "a@b and cd@ef", "\\2@\\1", 0, &n) == "b@a and ef@cd" && n == 2);
  EXPECT(sub(re, "a@b and cd@ef", "\\2@\\1", 1, &n) == "b@a and cd@ef" && n == 1); // count limits
  EXPECT(sub(re, "a@b", "<\\g<user>|\\g<2>|\\g<0>>", 0) == "<a|b|a@b>");
  EXPECT(sub(re, "a@b", "\\n\\t\\r\\f\\v\\a\\b\\\\", 0) == "\n\t\r\f\v\a\b\\");
  EXPECT(sub(re, "a@b", "\\.\\-", 0) == "\\.\\-");                                 // escaped punctuation keeps its backslash
  EXPECT(sub(re, "a@b", "\\101\\0", 0) == std::string("A\0", 2));                  // octal: one raw byte each
  EXPECT(sub(re, "no match", "x", 0, &n) == "no match" && n == 0);

  EXPECT(sub(re, "a@b", "x\\", 0) == "<error: bad escape (end of pattern)>");
  EXPECT(sub(re, "a@b", "\\477", 0) == "<error: octal escape value outside of range 0-0o377>");
  EXPECT(sub(re, "a@b", "\\9", 0) == "<error: invalid group reference>");
  EXPECT(sub(re, "a@b", "\\g<9>", 0) == "<error: invalid group reference>");
  EXPECT(sub(re, "a@b", "\\gx", 0) == "<error: missing < in \\g>");
  EXPECT(sub(re, "a@b", "\\g", 0) == "<error: missing < in \\g>");
  EXPECT(sub(re, "a@b", "\\g<>", 0) == "<error: missing group name in \\g<>>");
  EXPECT(sub(re, "a@b", "\\g<user", 0) == "<error: missing group name in \\g<>>");
  EXPECT(sub(re, "a@b", "\\g<1x>", 0) == "<error: bad character in group name>");
  EXPECT(sub(re, "a@b", "\\g<nobody>", 0) == "<error: unknown group name>");
  EXPECT(sub(re, "a@b", "\\q", 0) == "<error: bad escape in replacement>");
  EXPECT(sub(re, "a@b", "\\Q", 0) == "<error: bad escape in replacement>");

  // An unmatched optional group contributes nothing.
  real_regex* opt = compile("(a)(b)?", 0, code);
  EXPECT(sub(opt, "a", "[\\1\\2]", 0) == "[a]");
  real_free(opt);

  // A buffer shorter than the result receives a prefix and the full length is still returned.
  char small[3] = {'?', '?', '?'};
  EXPECT(real_sub(re, "a@b", 3, "\\2@\\1", 5, 0, small, sizeof small, nullptr, nullptr, 0) == 3);
  EXPECT(std::string(small, 3) == "b@a");
  char tiny[2] = {'?', '?'};
  EXPECT(real_sub(re, "ab@cd", 5, "\\2@\\1", 5, 0, tiny, sizeof tiny, nullptr, nullptr, 0) == 5);
  EXPECT(std::string(tiny, 2) == "cd");

  char err[64] = {0};
  EXPECT(real_sub(nullptr, "a", 1, "x", 1, 0, nullptr, 0, nullptr, err, sizeof err) == capi_error);
  EXPECT(std::string(err) == "null re/text/repl");
  EXPECT(real_sub(re, nullptr, 1, "x", 1, 0, nullptr, 0, nullptr, err, sizeof err) == capi_error);
  EXPECT(real_sub(re, "a", 1, nullptr, 1, 0, nullptr, 0, nullptr, err, sizeof err) == capi_error);
  EXPECT(real_sub(re, nullptr, 0, nullptr, 0, 0, nullptr, 0, nullptr, err, sizeof err) == 0);  // (NULL, 0) twice
  real_free(re);
}

TEST(capi_expand_checks_every_span)
{
  int         code = 0;
  real_regex* re   = compile("(\\w+)@(\\w+)", 0, code);
  const char* text = "a@b and cd@ef";
  std::size_t spans[6];
  EXPECT(real_match(re, text, 13, 5, 13, REAL_MODE_SEARCH, spans) == 1);
  EXPECT(expand(re, text, spans, 6, "\\2@\\1") == "ef@cd");                 // the match supplied, not the first
  EXPECT(expand(re, text, spans, 2, "\\1") == "<error: group reference beyond the spans supplied>");
  EXPECT(expand(re, text, spans, 6, "\\9") == "<error: invalid group reference>");

  const std::string span_msg   = "<error: span outside the subject, or inverted>";
  const std::size_t outside[6] =  {50, 100, spans[2], spans[3], spans[4], spans[5]};
  EXPECT(expand(re, text, outside, 6, "xyz") == span_msg);  // a pair the template never names
  const std::size_t inverted[6] = {spans[0], spans[1], 4, 1, spans[4], spans[5]};
  EXPECT(expand(re, text, inverted, 6, "lit") == span_msg);

  // An unmatched optional group is SIZE_MAX in both slots: skipped, referenced or not.
  real_regex* opt = compile("(a)(b)?", 0, code);
  std::size_t ospans[6];
  EXPECT(real_match(opt, "a", 1, 0, 1, REAL_MODE_SEARCH, ospans) == 1);
  EXPECT(expand(opt, "a", ospans, 6, "[\\1\\2]") == "[a]");
  EXPECT(expand(opt, "a", ospans, 6, "z") == "z");
  real_free(opt);

  char err[64] = {0};
  EXPECT(real_expand(nullptr, text, 13, spans, 6, "x", 1, nullptr, 0, err, sizeof err) == capi_error);
  EXPECT(std::string(err) == "null re/text/repl/spans");
  EXPECT(real_expand(re, text, 13, nullptr, 6, "x", 1, nullptr, 0, err, sizeof err) == capi_error);
  EXPECT(real_expand(re, text, 13, spans, 6, "\\q", 2, nullptr, 0, err, sizeof err) == capi_error);
  EXPECT(std::string(err) == "bad escape in replacement");
  real_free(re);
}

TEST(capi_set_queries)
{
  const char* const pats[3] = {"a+", "\\d", "z"};
  int               code    = 99;
  real_regex_set*   set     = real_set_compile(pats, nullptr, 3, 0, nullptr, 0, &code);  // NULL lens: strlen each
  EXPECT(set != nullptr && code == REAL_ERR_NONE);
  EXPECT(real_set_size(set) == 3);
  EXPECT(real_set_is_match(set, "xx7", 3) == 1);
  EXPECT(real_set_is_match(set, "xyw", 3) == 0);
  std::uint8_t hits[3] = {9, 9, 9};
  EXPECT(real_set_matches(set, "aa 5", 4, hits) == 0);
  EXPECT(hits[0] == 1 && hits[1] == 1 && hits[2] == 0);

  EXPECT(real_set_size(nullptr) == 0);
  EXPECT(real_set_is_match(nullptr, "a", 1) == -1);
  EXPECT(real_set_is_match(set, nullptr, 1) == -1);
  EXPECT(real_set_matches(nullptr, "a", 1, hits) == -1);
  EXPECT(real_set_matches(set, nullptr, 1, hits) == -1);
  EXPECT(real_set_matches(set, "a", 1, nullptr) == -1);
  real_set_free(set);
  real_set_free(nullptr);  // intentional no-op
}
