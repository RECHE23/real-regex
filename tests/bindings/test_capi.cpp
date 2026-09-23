//! The C ABI shim (bindings/c) exercised from the instrumented test binary, so real_capi.cpp is covered by
//! the coverage floor and run under ASan/UBSan in the sanitize build — the raw-pointer surface the Rust and
//! future bindings sit on. The pure-C-linkage smoke stays in bindings/c/test_capi.c.
#include <sciforge/test/framework.hpp>

#include <real_capi.h>

#include <real/real.hpp>

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
  constexpr std::size_t no_pos = static_cast<std::size_t>(-1);
  const char* const     pats[3] = {"a", "bc(d", "e"};
  const std::size_t     lens[3] = {1, 4, 1};
  const real::regex_error member = engine_error(pats[1]);
  char        err[256] = {0};
  int         code     = 99;
  std::size_t pos      = 12345;
  EXPECT(real_set_compile_ex(pats, lens, 3, 0, err, sizeof err, &code, &pos) == nullptr);
  EXPECT(pos == member.position());     // an offset inside the failing member
  EXPECT(code == REAL_ERR_SYNTAX);
  EXPECT(std::string(err).find("(in pattern 1 of 3)") != std::string::npos);  // which member: in the cause
  EXPECT(std::string(err).rfind("regex_error at", 0) != 0);                   // and no position prefix

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
