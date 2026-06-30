// Differential fuzz target: real::compat vs std::regex (ECMAScript) — the R4 net that catches
// silent divergences outside the curated corpus.
//
// Contract under test: when real::compat runs on the `real` backend and the pattern is not a
// known libstdc++/spec deviation, its verdict + whole-match span MUST equal std::regex's. Any
// non-allowlisted divergence aborts (libFuzzer reports the reproducer).
//
// Build & run: clang++ -std=c++20 -O1 -g -Iinclude -fsanitize=fuzzer,address,undefined \
//              fuzz/fuzz_compat.cpp -o fuzz_compat && ./fuzz_compat fuzz/corpus
//
// Allowlisted (real follows the ECMAScript spec; libstdc++ deviates — see COMPATIBILITY.md):
//   - POSIX bracket expressions `[[:...:]]` (libstdc++ non-spec extension)
//   - lookbehind `(?<=` / `(?<!` (libstdc++ does not implement it)
// and any pattern that takes the std fallback (then compat IS std, so no divergence is possible).

#include <real/std_compat.hpp>

#include <cstddef>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>

namespace {

  // A pattern the harness must not compare against std at all (real follows spec, libstdc++
  // deviates on it, so a both-accept span comparison would be meaningless).
  bool allowlisted(std::string_view pat)
  {
    return pat.find("[[:") != std::string_view::npos  // POSIX bracket expression (non-spec libstdc++ ext)
           || pat.find("(?<=") != std::string_view::npos // lookbehind (ES2018; libstdc++ lacks it)
           || pat.find("(?<!") != std::string_view::npos;
  }

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
  if (size < 2) {
    return 0;
  }
  const std::size_t      remaining {size - 2};
  const std::size_t      pattern_len {data[1] % (remaining + 1U)};
  const auto*            body {reinterpret_cast<const char*>(data + 2)};
  const std::string      pattern(body, pattern_len);
  const std::string      subject(body + pattern_len, remaining - pattern_len);

  if (allowlisted(pattern)) {
    return 0;
  }

  real::compat::regex compat;
  try {
    compat = real::compat::regex(pattern);
  }
  catch (const std::regex_error&) {
    return 0; // invalid for both backends — not a divergence
  }

  if (!compat.uses_real()) {
    return 0; // fallback path: compat IS std, nothing to compare
  }

  std::regex std_re;
  try {
    std_re = std::regex(pattern, std::regex::ECMAScript);
  }
  catch (const std::regex_error&) {
    // real accepts, std rejects: a benign superset. real follows the ECMAScript spec (incl. Annex B
    // web-compat — a malformed `{` is a literal, identity escapes like `\A` are literals, lookbehind
    // is supported), where libstdc++ is *stricter* and rejects. There is no std result to diverge
    // from, so skip. (Spec-conformance of these patterns is pinned by the test suite + corpus, the
    // primary ECMAScript oracle — not by this std differential, which has no spec engine.)
    return 0;
  }

  real::compat::smatch cm;
  std::smatch          sm;
  const bool           cf {real::compat::regex_search(subject, cm, compat)};
  const bool           sf {std::regex_search(subject, sm, std_re)};

  if (cf != sf) {
    __builtin_trap(); // verdict divergence on a real-backed, non-allowlisted pattern
  }
  if (cf && (cm.position(0) != sm.position(0) || cm.length(0) != sm.length(0))) {
    __builtin_trap(); // whole-match span divergence
  }
  return 0;
}
