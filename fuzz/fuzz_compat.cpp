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
#include <utility>
#include <vector>

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

  // S2 net: regex_replace must match std::regex_replace — the empty-match TRAVERSAL is the new risk.
  // A nullable real-backed pattern routes to the lazy std backend (so it equals std by construction);
  // a non-nullable one runs the compat substitution on real's traversal, which must agree with std.
  // The format exercises $&, $1, $`, $', $$ and a literal.
  static const std::string fmt {"<$&|$1|$`|$'|$$x>"};
  const std::string compat_out {real::compat::regex_replace(subject, compat, fmt)};
  const std::string std_out {std::regex_replace(subject, std_re, fmt)};
  if (compat_out != std_out) {
    __builtin_trap(); // regex_replace divergence (format or empty-match traversal)
  }

  // S2b net: the SEQUENCE of match spans from the iterator must equal std::sregex_iterator's — not
  // just the first match. The empty-match advancement is the risk; a nullable real-backed pattern
  // routes the iterator to the lazy std backend, a non-nullable one drives real's region traversal.
  using span = std::pair<long, long>;
  std::vector<span> compat_spans;
  for (real::compat::sregex_iterator it(subject.begin(), subject.end(), compat), end; it != end;
       ++it) {
    compat_spans.emplace_back(it->position(0), it->length(0));
  }
  std::vector<span> std_spans;
  for (std::sregex_iterator it(subject.begin(), subject.end(), std_re), end; it != end; ++it) {
    std_spans.emplace_back(it->position(0), it->length(0));
  }
  if (compat_spans != std_spans) {
    __builtin_trap(); // iterator span-sequence divergence (empty-match traversal)
  }

  // S2b-2 net: the token SEQUENCE from regex_token_iterator must equal std::sregex_token_iterator's.
  // The -1 (split) field is the divergence-prone one: empty fields between adjacent matches are
  // produced, the trailing suffix only when non-empty, and a no-match yields the whole sequence.
  // Compare (str, matched) so the participation flag is pinned too.
  using token = std::pair<std::string, bool>;
  for (const int field : {-1, 0}) {
    std::vector<token> compat_tokens;
    for (real::compat::sregex_token_iterator it(subject.begin(), subject.end(), compat, field), end;
         it != end; ++it) {
      compat_tokens.emplace_back(it->str(), it->matched);
    }
    std::vector<token> std_tokens;
    for (std::sregex_token_iterator it(subject.begin(), subject.end(), std_re, field), end;
         it != end; ++it) {
      std_tokens.emplace_back(it->str(), it->matched);
    }
    if (compat_tokens != std_tokens) {
      __builtin_trap(); // token-sequence divergence (split fields / trailing suffix derivation)
    }
  }
  return 0;
}
