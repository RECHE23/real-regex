/*
 * real_capi.cpp — the C ABI implementation (compiles the header-only engine, exposes extern "C" handles).
 * No C++ exception crosses the boundary: every entry point catches and reports via return value / errbuf.
 */
#include "real_capi.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <real/real.hpp>
#include <real/regex_set.hpp>

namespace {
  using dyn_iter = decltype(std::declval<const real::regex&>().find_iter(std::string_view {}).begin());
}

struct real_regex
{
  real::regex rx;
};

struct real_iter
{
  dyn_iter it;
  dyn_iter end;
};

struct real_regex_set
{
  real::regex_set set;
};

extern "C" {

namespace {
  // Copy `what` into `errbuf` (NUL-terminated, truncating). memcpy + explicit NUL rather than strncpy: MSVC
  // deprecates strncpy (C4996) and memcpy is not on that list, so this needs no _CRT_SECURE_NO_WARNINGS — the
  // house no-suppression style holds at the shim too.
  void write_err(char* errbuf, size_t errbuf_len, const char* what)
  {
    if (errbuf != nullptr && errbuf_len > 0) {
      const size_t n = std::min(std::strlen(what), errbuf_len - 1);
      std::memcpy(errbuf, what, n);
      errbuf[n] = '\0';
    }
  }
}

real_regex* real_compile(const char* pattern, size_t len, uint32_t flags,
                         char* errbuf, size_t errbuf_len, int* code)
{
  if (code != nullptr) {
    *code = REAL_ERR_NONE;
  }
  try {
    return new real_regex {real::regex(std::string_view(pattern, len), static_cast<real::flags>(flags))};
  }
  catch (const real::regex_error& e) {
    // The engine tags whether the pattern is malformed or merely unsupported — pass that through as a code,
    // so the binding never classifies on the message text.
    if (code != nullptr) {
      *code = (e.kind() == real::error_kind::unsupported) ? REAL_ERR_UNSUPPORTED : REAL_ERR_SYNTAX;
    }
    write_err(errbuf, errbuf_len, e.what());
    return nullptr;
  }
  catch (const std::exception& e) {
    if (code != nullptr) {
      *code = REAL_ERR_SYNTAX;
    }
    write_err(errbuf, errbuf_len, e.what());
    return nullptr;
  }
  catch (...) {
    if (code != nullptr) {
      *code = REAL_ERR_SYNTAX;
    }
    write_err(errbuf, errbuf_len, "unknown error");
    return nullptr;
  }
}

size_t real_group_count(const real_regex* re)
{
  return re->rx.group_count() + 1; // group_count() excludes group 0
}

size_t real_group_name(const real_regex* re, size_t group, char* buf, size_t buflen)
{
  for (const auto& [name, number] : re->rx.named_groups()) {
    if (number == group) {
      if (buf != nullptr && buflen > 0) {
        const size_t n {name.size() < (buflen - 1) ? name.size() : (buflen - 1)};
        std::memcpy(buf, name.data(), n);
        buf[n] = '\0';
      }
      return name.size();
    }
  }
  if (buf != nullptr && buflen > 0) {
    buf[0] = '\0';
  }
  return 0; // unnamed (or group 0 / out of range)
}

void real_free(real_regex* re)
{
  delete re;
}

real_iter* real_find_iter(const real_regex* re, const char* text, size_t len)
{
  try {
    auto range = re->rx.find_iter(std::string_view(text, len));
    return new real_iter {range.begin(), range.end()};
  }
  catch (...) {
    return nullptr;
  }
}

real_iter* real_find_iter_at(const real_regex* re, const char* text, size_t len, size_t start)
{
  try {
    auto range = re->rx.find_iter(std::string_view(text, len), start, len);
    return new real_iter {range.begin(), range.end()};
  }
  catch (...) {
    return nullptr;
  }
}

int real_iter_next(real_iter* iter, size_t* spans)
{
  if (iter == nullptr) {
    return -1; // a null cursor means the iterator failed to construct — never dereference it
  }
  try {
    if (iter->it == iter->end) {
      return 0;
    }
    const auto& m {*iter->it};
    for (size_t g = 0; g < m.size(); ++g) {
      spans[2 * g]       = m.start(g);
      spans[(2 * g) + 1] = m.end(g);
    }
    ++iter->it;
    return 1;
  }
  catch (...) {
    return -1; // no C++ exception may cross into C — report an internal error instead of terminating
  }
}

void real_iter_free(real_iter* iter)
{
  delete iter;
}

real_regex_set* real_set_compile(const char* const* patterns, const size_t* lens, size_t n,
                                 uint32_t flags, char* errbuf, size_t errbuf_len, int* code)
{
  if (code != nullptr) {
    *code = REAL_ERR_NONE;
  }
  if (patterns == nullptr && n > 0) {
    if (code != nullptr) {
      *code = REAL_ERR_SYNTAX;
    }
    write_err(errbuf, errbuf_len, "null patterns");
    return nullptr;
  }
  try {
    std::vector<std::string_view> views;
    views.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      const size_t len = (lens != nullptr) ? lens[i] : std::strlen(patterns[i]);
      views.emplace_back(patterns[i], len);
    }
    return new real_regex_set {
      real::regex_set(std::span<const std::string_view> {views}, static_cast<real::flags>(flags))};
  }
  catch (const real::regex_error& e) {
    if (code != nullptr) {
      *code = (e.kind() == real::error_kind::unsupported) ? REAL_ERR_UNSUPPORTED : REAL_ERR_SYNTAX;
    }
    write_err(errbuf, errbuf_len, e.what());
    return nullptr;
  }
  catch (const std::exception& e) {
    if (code != nullptr) {
      *code = REAL_ERR_SYNTAX;
    }
    write_err(errbuf, errbuf_len, e.what());
    return nullptr;
  }
  catch (...) {
    if (code != nullptr) {
      *code = REAL_ERR_SYNTAX;
    }
    write_err(errbuf, errbuf_len, "unknown error");
    return nullptr;
  }
}

size_t real_set_size(const real_regex_set* set)
{
  return set == nullptr ? 0 : set->set.size();
}

void real_set_free(real_regex_set* set)
{
  delete set;
}

int real_set_is_match(const real_regex_set* set, const char* text, size_t len)
{
  if (set == nullptr || text == nullptr) {
    return -1;
  }
  try {
    return set->set.is_match(std::string_view(text, len)) ? 1 : 0;
  }
  catch (...) {
    return -1;
  }
}

int real_set_matches(const real_regex_set* set, const char* text, size_t len, uint8_t* out)
{
  if (set == nullptr || text == nullptr || out == nullptr) {
    return -1;
  }
  try {
    const auto hit = set->set.matches(std::string_view(text, len));
    for (size_t i = 0; i < hit.size(); ++i) {
      out[i] = hit[i] ? 1 : 0;
    }
    return 0;
  }
  catch (...) {
    return -1;
  }
}

} // extern "C"
