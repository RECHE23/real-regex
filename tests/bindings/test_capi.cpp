//! The C ABI shim (bindings/c) exercised from the instrumented test binary, so real_capi.cpp is covered by
//! the coverage floor and run under ASan/UBSan in the sanitize build — the raw-pointer surface the Rust and
//! future bindings sit on. The pure-C-linkage smoke stays in bindings/c/test_capi.c.
#include <sciforge/test/framework.hpp>

#include <real_capi.h>

#include <cstdint>
#include <cstring>
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
