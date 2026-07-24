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
  if (re == nullptr) {
    return 0; // invalid handle: zero span slots (documented in real_capi.h)
  }
  return re->rx.group_count() + 1; // group_count() excludes group 0
}

size_t real_group_name(const real_regex* re, size_t group, char* buf, size_t buflen)
{
  if (re == nullptr) {
    if (buf != nullptr && buflen > 0) {
      buf[0] = '\0';
    }
    return 0; // invalid handle: empty name (documented in real_capi.h)
  }
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
  // Intentionally null-safe: `delete nullptr` is a C++ no-op. Callers (Go Close after
  // already-nil, defensive free paths) may pass NULL — do not "fix" this with a guard that
  // would make null free a special case elsewhere.
  delete re;
}

real_iter* real_find_iter(const real_regex* re, const char* text, size_t len)
{
  if (re == nullptr) {
    return nullptr; // invalid handle (documented in real_capi.h)
  }
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
  if (re == nullptr) {
    return nullptr;
  }
  try {
    auto range = re->rx.find_iter(std::string_view(text, len), start, len);
    return new real_iter {range.begin(), range.end()};
  }
  catch (...) {
    return nullptr;
  }
}

real_iter* real_find_iter_between(const real_regex* re, const char* text, size_t len, size_t start, size_t end)
{
  if (re == nullptr) {
    return nullptr;
  }
  try {
    auto range = re->rx.find_iter(std::string_view(text, len), start, end);
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
    // Flattened slots_ is already [start0,end0,…] — one memcpy, not per-group start/end.
    if (spans != nullptr) {
      const auto flat {m.spans()};
      if (!flat.empty()) {
        std::memcpy(spans, flat.data(), flat.size() * sizeof(size_t));
      }
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

size_t real_count_matches(const real_regex* re, const char* text, size_t len)
{
  if (re == nullptr || (text == nullptr && len != 0)) {
    return static_cast<size_t>(-1);
  }
  try {
    return re->rx.count_matches(std::string_view(text, len));
  }
  catch (...) {
    return static_cast<size_t>(-1);
  }
}

int real_match(const real_regex* re, const char* text, size_t len,
               size_t start, size_t end, int mode, size_t* spans)
{
  if (re == nullptr || (text == nullptr && len != 0)) {
    return -1;
  }
  try {
    const std::string_view sv(text, len);
    const auto             run = [&] {
                                    switch (mode) {
                                      case REAL_MODE_MATCH:      return re->rx.match(sv, start, end);
                                      case REAL_MODE_FULLMATCH:  return re->rx.fullmatch(sv, start, end);
                                      case REAL_MODE_SEARCH:
                                      default:                   return re->rx.search(sv, start, end);
                                    }
                                  };
    const auto result = run();
    if (!result.matched()) {
      return 0;
    }
    if (spans != nullptr) {
      const auto flat {result.spans()};
      if (!flat.empty()) {
        std::memcpy(spans, flat.data(), flat.size() * sizeof(size_t));
      }
    }
    return 1;
  }
  catch (...) {
    return -1;
  }
}

namespace {
  // One parsed piece of a sub() replacement template: literal bytes, or a group reference.
  // Mirrors bindings/python/src/_real.cpp's own repl_segment/parse_template — same proven
  // syntax, minus the Python C-API coupling. This ABI is byte-oriented (no str/bytes split),
  // so an octal escape is always ONE raw byte, never re-encoded as UTF-8 (unconditionally the
  // Python version's `is_bytes` branch).
  struct sub_segment
  {
    std::string literal;
    long long   group {-1}; // -1 = literal-only segment
  };

  bool parse_sub_template(const real_regex* re, std::string_view repl,
                          std::vector<sub_segment>& out, std::string& err)
  {
    std::string literal;
    const auto  flush_group = [&](long long group) {
                                 out.push_back({std::move(literal), -1});
                                 literal.clear();
                                 out.push_back({std::string(), group});
                               };
    std::size_t i {0};
    while (i < repl.size()) {
      const char ch {repl[i]};
      if (ch != '\\') {
        literal.push_back(ch);
        ++i;
        continue;
      }
      ++i;
      if (i >= repl.size()) {
        err = "bad escape (end of pattern)";
        return false;
      }
      const char next_ch {repl[i]};
      if (next_ch >= '0' && next_ch <= '9') {
        const real::detail::digit_escape_result decoded {real::detail::decode_digit_escape(repl, i)};
        i += decoded.length;
        if (decoded.kind == real::detail::digit_escape_kind::octal_overflow) {
          err = "octal escape value outside of range 0-0o377";
          return false;
        }
        if (decoded.kind == real::detail::digit_escape_kind::octal) {
          literal.push_back(static_cast<char>(decoded.value));
          continue;
        }
        const long long group {static_cast<long long>(decoded.value)};
        if (static_cast<std::size_t>(group) > re->rx.group_count()) {
          err = "invalid group reference";
          return false;
        }
        flush_group(group);
        continue;
      }
      if (next_ch == 'g') {
        ++i;
        if (i >= repl.size() || repl[i] != '<') {
          err = "missing < in \\g";
          return false;
        }
        const std::size_t name_begin {++i};
        while (i < repl.size() && repl[i] != '>') {
          ++i;
        }
        if (i == repl.size() || i == name_begin) {
          err = "missing group name in \\g<>";
          return false;
        }
        const std::string_view name {repl.substr(name_begin, i - name_begin)};
        ++i; // consume '>'
        long long group {-1};
        if (name[0] >= '0' && name[0] <= '9') {
          group = 0;
          for (const char digit : name) {
            if (digit < '0' || digit > '9') {
              err = "bad character in group name";
              return false;
            }
            group = (group * 10) + (digit - '0');
          }
        }
        else {
          const std::size_t named {re->rx.group_index(name)};
          if (named == real::npos) {
            err = "unknown group name";
            return false;
          }
          group = static_cast<long long>(named);
        }
        if (static_cast<std::size_t>(group) > re->rx.group_count()) {
          err = "invalid group reference";
          return false;
        }
        flush_group(group);
        continue;
      }
      ++i;
      switch (next_ch) {
        case 'n':  literal.push_back('\n'); break;
        case 't':  literal.push_back('\t'); break;
        case 'r':  literal.push_back('\r'); break;
        case 'f':  literal.push_back('\f'); break;
        case 'v':  literal.push_back('\v'); break;
        case 'a':  literal.push_back('\a'); break;
        case 'b':  literal.push_back('\b'); break;
        case '\\': literal.push_back('\\'); break;
        default:
          // Like Python: unknown letter escapes are errors, escaped punctuation keeps the backslash.
          if ((next_ch >= 'A' && next_ch <= 'Z') || (next_ch >= 'a' && next_ch <= 'z')) {
            err = "bad escape in replacement";
            return false;
          }
          literal.push_back('\\');
          literal.push_back(next_ch);
          break;
      }
    }
    out.push_back({std::move(literal), -1});
    return true;
  }
} // namespace

size_t real_sub(const real_regex* re, const char* text, size_t len,
                const char* repl, size_t repl_len, size_t count,
                char* out, size_t outlen, size_t* n_subs,
                char* errbuf, size_t errbuf_len)
{
  if (re == nullptr || (text == nullptr && len != 0) || (repl == nullptr && repl_len != 0)) {
    write_err(errbuf, errbuf_len, "null re/text/repl");
    return static_cast<size_t>(-1);
  }
  std::vector<sub_segment> segments;
  std::string              parse_err;
  if (!parse_sub_template(re, std::string_view(repl, repl_len), segments, parse_err)) {
    write_err(errbuf, errbuf_len, parse_err.c_str());
    return static_cast<size_t>(-1);
  }
  try {
    const std::string_view subject(text, len);
    std::string             result;
    std::size_t              last {0};
    std::size_t              done {0};
    for (const auto& m : re->rx.find_iter(subject)) {
      if (count != 0 && done == count) {
        break;
      }
      result.append(subject.data() + last, m.start() - last);
      for (const auto& seg : segments) {
        if (seg.group < 0) {
          result.append(seg.literal);
        }
        else {
          const std::size_t g {static_cast<std::size_t>(seg.group)};
          const std::size_t s {m.start(g)};
          if (s != real::npos) {
            result.append(subject.data() + s, m.end(g) - s);
          }
        }
      }
      last = m.end();
      ++done;
    }
    result.append(subject.data() + last, len - last);
    if (n_subs != nullptr) {
      *n_subs = done;
    }
    if (out != nullptr && outlen > 0) {
      const std::size_t n {std::min(result.size(), outlen)};
      std::memcpy(out, result.data(), n);
    }
    return result.size();
  }
  catch (...) {
    write_err(errbuf, errbuf_len, "internal engine error");
    return static_cast<size_t>(-1);
  }
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
  if (set == nullptr || (text == nullptr && len != 0)) {
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
  if (set == nullptr || (text == nullptr && len != 0) || out == nullptr) {
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
