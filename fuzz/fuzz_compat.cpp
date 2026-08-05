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

#include <real/compat/std/regex.hpp>

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
    // `\b`/`\B` anywhere: std::regex (libstdc++ AND libc++) mis-evaluates word boundaries in at
    // least two independent ways — `[a-z]{4,}(?=\b)` on "dacd1..." matches under std though
    // 'd'→'1' is word→word (ECMAScript says none), and bare `\B` on "_" matches under std though
    // both edges of "_" are boundaries. node and CPython agree with real on both. std is not an
    // oracle for word-boundary patterns; the spec behavior is pinned in
    // tests/compat/test_compat.cpp. (The `\b` search also catches the class form `[\b]`
    // (backspace) — over-wide, which only skips, never falsely traps.)
    return pat.find("[[:") != std::string_view::npos  // POSIX bracket expression (non-spec libstdc++ ext)
           || pat.find("(?<=") != std::string_view::npos // lookbehind (ES2018; libstdc++ lacks it)
           || pat.find("(?<!") != std::string_view::npos
           || pat.find("\\b") != std::string_view::npos
           || pat.find("\\B") != std::string_view::npos;
  }

  // Light pre-parse (not a full lexer -- just enough to bound the smoke's allocator load before
  // compiling): sums the counted-repeat operands in every `{n}` / `{n,}` / `{n,m}` in `pat`, skipping
  // `\{` (an escaped brace is a literal, not a quantifier start, under the Annex-B grammar real
  // follows). A malformed `{...}` still gets its digits summed here even though it is itself a literal
  // (no real quantifier) -- harmless, it only makes the bound stricter, never looser.
  std::size_t counted_repeat_load(std::string_view pat)
  {
    std::size_t sum {};
    for (std::size_t i = 0; i < pat.size(); ++i) {
      if (pat[i] == '\\') {
        ++i; // skip the escaped character -- \{ is a literal brace, not a quantifier
        continue;
      }
      if (pat[i] != '{') {
        continue;
      }
      std::size_t      j {i + 1};
      const auto        read_number {[&]() -> std::size_t {
                          std::size_t v {};
                          while (j < pat.size() && pat[j] >= '0' && pat[j] <= '9') {
                            v = (v * 10) + static_cast<std::size_t>(pat[j] - '0');
                            ++j;
                          }
                          return v;
                        }};
      sum += read_number();
      if (j < pat.size() && pat[j] == ',') {
        ++j;
        sum += read_number();
      }
    }
    return sum;
  }

  // The sanitized smoke's job is divergences, not ASAN-allocator benchmarking: a larger counted
  // repeat adds no NEW divergence class (spans are pinned by the dedicated {n}/{n,m} tests), it only
  // multiplies allocation traffic -- which ASAN instruments heavily enough to amplify a `{999}`-class
  // pattern from a flat ~0.13 ms native run into a many-second smoke, with no new bug behind it.
  constexpr std::size_t max_counted_repeat_load {256};

  // A quantifier applied to a group that itself contains an unbounded quantifier -- `(a+)+`, `(a*)*`,
  // `(?:x+)*` -- is the shape that makes a BACKTRACKER exponential. real is linear and does not care;
  // std::regex is the oracle here, and it does. Its safety valve (error_complexity / error_stack,
  // already skipped at match time below) does NOT fire on this shape: libstdc++ simply keeps working,
  // and the smoke run died on exactly that -- `libFuzzer: timeout after 12 seconds` with all 256 stack
  // frames inside std::__detail::_Executor, on a corpus entry the fuzzer reached only after a codegen
  // change moved its coverage. Nothing in real was wrong; the ORACLE ran out of time.
  //
  // Scanned, not parsed: a `)` (or `]`) directly followed by `*`, `+`, or `{`, with some unbounded
  // quantifier seen since the matching opener. Escapes and classes are skipped so `\)` and `[+]` do
  // not count. Approximate on purpose -- a false positive only shortens a subject.
  bool nested_unbounded_quantifier(std::string_view pat)
  {
    int  depth      {0};
    int  quant_at   {-1}; // depth at which an unbounded quantifier was last seen, -1 for none
    bool in_class   {false};
    for (std::size_t i = 0; i < pat.size(); ++i) {
      const char c {pat[i]};
      if (c == '\\') {
        ++i;
        continue;
      }
      if (in_class) {
        in_class = (c != ']');
        continue;
      }
      if (c == '[') {
        in_class = true;
      }
      else if (c == '(') {
        ++depth;
      }
      else if (c == ')') {
        const bool quantified {i + 1 < pat.size()
                               && (pat[i + 1] == '*' || pat[i + 1] == '+' || pat[i + 1] == '{')};
        if (quantified && quant_at >= depth) {
          return true; // an unbounded quantifier inside a group that is itself quantified
        }
        if (quant_at >= depth) {
          quant_at = depth - 1; // that inner quantifier no longer nests once the group closes
        }
        if (depth > 0) {
          --depth;
        }
      }
      else if (c == '*' || c == '+') {
        quant_at = depth;
      }
    }
    return false;
  }

  // Long enough for a backtracker to go exponential, short enough that 2^n stays trivial. Only
  // applied to the shape above, so ordinary patterns keep the full fuzzer-chosen subject.
  constexpr std::size_t max_nested_quantifier_subject {16};

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
  if (counted_repeat_load(pattern) > max_counted_repeat_load) {
    return 0; // ASAN-allocator load, not a new divergence class -- see counted_repeat_load's comment
  }
  if (subject.size() > max_nested_quantifier_subject && nested_unbounded_quantifier(pattern)) {
    return 0; // std, the oracle, has no bound for this shape -- see nested_unbounded_quantifier
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

  // std::regex can also throw AT MATCH TIME (error_complexity / error_stack — its backtracker's
  // safety valve, on alternation/optional piles). real, linear, has no such mode; when std runs
  // out of budget there is no oracle answer, so the comparison is skipped. compat's own std-routed
  // pieces (e.g. `$0` formats) may throw the same way — same skip, same reason.
  try {

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
  // S5b: the format is GENERATED from the input (not fixed) so it exercises $0, $N/$NN, $&, $`, $',
  // $$, a lone trailing $, and literals — the branches that were coverage-discovered before.
  std::string fmt;
  for (const char c : subject) {
    switch (static_cast<unsigned char>(c) % 12U) {
      case 0: fmt += "$&"; break;
      case 1: fmt += "$`"; break;
      case 2: fmt += "$'"; break;
      case 3: fmt += "$$"; break;
      case 4: fmt += "$0"; break;  // platform-variant -> compat routes to std
      case 5: fmt += "$1"; break;
      case 6: fmt += "$2"; break;
      case 7: fmt += "$15"; break; // multi-digit, usually out-of-range group
      case 8: fmt += "$99"; break;
      case 9: fmt += "$"; break;   // lone dollar
      case 10: fmt += 'x'; break;  // literal
      default: fmt += c; break;    // raw byte literal
    }
    if (fmt.size() > 96U) {
      break;
    }
  }
  const std::string compat_out {real::compat::regex_replace(subject, compat, fmt)};
  const std::string std_out {std::regex_replace(subject, std_re, fmt)};
  if (compat_out != std_out) {
    __builtin_trap(); // regex_replace divergence (format expansion or empty-match traversal)
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

  // S5b: a GENERATED field list — mixed -1/group/out-of-range selectors — must match
  // std::sregex_token_iterator's token sequence (this is on libstdc++, the standard-conformant one).
  {
    static constexpr int menu[] {-1, 0, 1, 2, 3, 5};
    std::vector<int>     fields;
    const std::size_t    field_count {1U + (static_cast<std::size_t>(data[0]) % 3U)};
    for (std::size_t k = 0; k < field_count; ++k) {
      fields.push_back(menu[(static_cast<std::size_t>(data[1]) + k) % (sizeof(menu) / sizeof(int))]);
    }
    // libc++-vs-libstdc++ divergence, isolated with std::regex alone (real::compat/REAL absent from the
    // repro -- e.g. "a" against "xxx" with fields {0,-1}): on a subject with ZERO matches, a MULTI-element
    // field list containing -1 yields the whole subject as one unmatched token on libstdc++ (what compat
    // targets) but an EMPTY sequence on libc++. The single-element {-1} case (the S2b-2 net above) agrees
    // on both ISAs -- only the multi-element list's no-match fallback differs, so only that cell is
    // skipped; every field list on a subject that DOES match is unaffected and still compared.
    if (!(fields.size() > 1 && !sf && [&] {
          for (const int f : fields) {
            if (f == -1) { return true; }
          }
          return false;
        }())) {
      std::vector<token> compat_list;
      for (real::compat::sregex_token_iterator it(subject.begin(), subject.end(), compat, fields), end;
           it != end; ++it) {
        compat_list.emplace_back(it->str(), it->matched);
      }
      std::vector<token> std_list;
      for (std::sregex_token_iterator it(subject.begin(), subject.end(), std_re, fields), end;
           it != end; ++it) {
        std_list.emplace_back(it->str(), it->matched);
      }
      if (compat_list != std_list) {
        __builtin_trap(); // token field-list divergence (mixed / out-of-range selectors)
      }
    }
  }

  // S3 net: a random subset of match flags must produce identical results compat(mf) vs std(mf) on
  // search + match + iterate. A constraining flag mis-categorized as honored-in-real (real_honors
  // letting it stay on real) would diverge here. data[0] (otherwise unused) drives the flag subset.
  namespace rcc = real::compat::regex_constants;
  namespace sc  = std::regex_constants;
  const std::uint8_t   fbits {data[0]};
  auto                 cmf {rcc::match_default};
  auto                 smf {sc::match_default};
  if ((fbits & 0x01U) != 0U) { cmf = cmf | rcc::match_not_bol; smf |= sc::match_not_bol; }
  if ((fbits & 0x02U) != 0U) { cmf = cmf | rcc::match_not_eol; smf |= sc::match_not_eol; }
  if ((fbits & 0x04U) != 0U) { cmf = cmf | rcc::match_not_bow; smf |= sc::match_not_bow; }
  if ((fbits & 0x08U) != 0U) { cmf = cmf | rcc::match_not_eow; smf |= sc::match_not_eow; }
  if ((fbits & 0x10U) != 0U) { cmf = cmf | rcc::match_continuous; smf |= sc::match_continuous; }
  if ((fbits & 0x20U) != 0U) { cmf = cmf | rcc::match_not_null; smf |= sc::match_not_null; }
  if ((fbits & 0x40U) != 0U) { cmf = cmf | rcc::match_any; smf |= sc::match_any; }
  // match_prev_avail requires *(first-1) to be valid: search a sub-range starting one past begin.
  const bool prev_avail {(fbits & 0x80U) != 0U && !subject.empty()};
  if (prev_avail) { cmf = cmf | rcc::match_prev_avail; smf |= sc::match_prev_avail; }
  const auto lo {prev_avail ? subject.begin() + 1 : subject.begin()};

  real::compat::smatch cmf_m;
  std::smatch          smf_m;
  if (real::compat::regex_search(lo, subject.end(), cmf_m, compat, cmf)
      != std::regex_search(lo, subject.end(), smf_m, std_re, smf)) {
    __builtin_trap(); // verdict divergence under match flags
  }
  if (cmf_m.ready() && smf_m.ready() && cmf_m[0].matched && smf_m[0].matched
      && (cmf_m.position(0) != smf_m.position(0) || cmf_m.length(0) != smf_m.length(0))) {
    __builtin_trap(); // span divergence under match flags
  }
  if (real::compat::regex_match(lo, subject.end(), compat, cmf)
      != std::regex_match(lo, subject.end(), std_re, smf)) {
    __builtin_trap(); // regex_match verdict divergence under match flags
  }
  std::vector<span> compat_mf_spans;
  for (real::compat::sregex_iterator it(lo, subject.end(), compat, cmf), end; it != end; ++it) {
    compat_mf_spans.emplace_back(it->position(0), it->length(0));
  }
  std::vector<span> std_mf_spans;
  for (std::sregex_iterator it(lo, subject.end(), std_re, smf), end; it != end; ++it) {
    std_mf_spans.emplace_back(it->position(0), it->length(0));
  }
  if (compat_mf_spans != std_mf_spans) {
    __builtin_trap(); // iterator span-sequence divergence under match flags
  }

  }
  catch (const std::regex_error&) {
    return 0; // std ran out of matching budget mid-comparison: no oracle answer for this input
  }
  return 0;
}
