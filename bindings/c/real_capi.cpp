/*
 * real_capi.cpp — the C ABI implementation (compiles the header-only engine, exposes extern "C" handles).
 * No C++ exception crosses the boundary: every entry point catches and reports via return value / errbuf.
 */
#include "real_capi.h"

#include <cstring>
#include <exception>
#include <new>
#include <string_view>
#include <utility>

#include <real/real.hpp>

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

extern "C" {

real_regex* real_compile(const char* pattern, size_t len, uint32_t flags, char* errbuf, size_t errbuf_len)
{
  try {
    return new real_regex {real::regex(std::string_view(pattern, len), static_cast<real::flags>(flags))};
  }
  catch (const std::exception& e) {
    if (errbuf != nullptr && errbuf_len > 0) {
      std::strncpy(errbuf, e.what(), errbuf_len - 1);
      errbuf[errbuf_len - 1] = '\0';
    }
    return nullptr;
  }
  catch (...) {
    if (errbuf != nullptr && errbuf_len > 0) {
      std::strncpy(errbuf, "unknown error", errbuf_len - 1);
      errbuf[errbuf_len - 1] = '\0';
    }
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

void real_iter_free(real_iter* iter)
{
  delete iter;
}

} // extern "C"
