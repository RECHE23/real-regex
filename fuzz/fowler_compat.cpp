// Fowler / AT&T POSIX conformance of the compat layer: the three vendored testregex corpora
// (basic / nullsubexpr / repetition) run through real::compat vs the LOCAL std::regex, arbitrated three
// ways against the corpus's own POSIX-longest expectation. Each (case, grammar) lands in one bucket:
//
//   b1  REAL == std == Fowler        perfect
//   b2  REAL == std != Fowler        a shared std-quirk (the drop-in holds; POSIX-truth differs) — a table
//   b3  REAL == Fowler != std        a std bug (possibly per-platform) — a table
//   b4  REAL alone diverges          a REAL bug or a must-decline — MUST stay empty
//   delegated                        real declined -> std (== std by construction)
//   both_reject                      both engines reject the pattern (they agree it is invalid)
//   std_only_reject                  std rejects, real routes — REAL over-accepts; MUST stay empty
//
// The hard invariants (lib-stable): b3 == 0 and b4 == 0 (REAL agrees with std on every routed case),
// std_only == 0, the b1 count, and the per-file parsed-case counts (a "no silent caps" pin — a parser
// regression that drops lines must bite, since that is exactly the load_cases_dat bug this replaces).
// b2 / delegated / both_reject are reported columns; their split moves with the platform (e.g. `\}` is
// delegated on libc++, both-reject on libstdc++), so they are not pinned by raw equality between libs.
//
// The parser is the reference decoder for what these three .dat files actually use: the B/E grammars,
// the `i` (icase) flag, a `:HA#…:` annotation prefix, the SAME directive, NULL/NIL inputs, and the
// C-escapes (\n \t \r \f \v \a, \xHH, and octal — in a pattern, \1-\9 stay as backreferences).
//
// Usage: fowler_compat [<corpus-dir>]   (default: tests/corpora/fowler)

#include <cctype>
#include <cstdio>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "real/compat/std/regex.hpp"

namespace rc = real::compat;

namespace {

  using span = std::pair<long, long>; // {-1,-1} == no match

  int hex_value(char c)
  {
    return (c <= '9') ? (c - '0') : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
  }

  // Decode the AT&T C-escapes. In a pattern (\p is_pattern) the octal digits 1-9 stay as regex
  // backreferences; everywhere else \0-\7 is an octal byte.
  std::string decode(const std::string& s, bool is_pattern)
  {
    std::string out;
    std::size_t i {0};
    while (i < s.size()) {
      if (s[i] == '\\' && i + 1 < s.size()) {
        const char d {s[i + 1]};
        if (d == 'n') { out += '\n'; i += 2; continue; }
        if (d == 't') { out += '\t'; i += 2; continue; }
        if (d == 'r') { out += '\r'; i += 2; continue; }
        if (d == 'f') { out += '\f'; i += 2; continue; }
        if (d == 'v') { out += '\v'; i += 2; continue; }
        if (d == 'a') { out += '\a'; i += 2; continue; }
        if (d == '\\') { out += '\\'; i += 2; continue; }
        if (d == 'x') {
          unsigned    v {0};
          std::size_t k {i + 2};
          std::size_t n {0};
          while (k < s.size() && n < 2 && (std::isxdigit(static_cast<unsigned char>(s[k])) != 0)) {
            v = (v * 16U) + static_cast<unsigned>(hex_value(s[k]));
            ++k;
            ++n;
          }
          if (n > 0) { out += static_cast<char>(v); i = k; continue; }
        }
        if (d == '0' || (!is_pattern && d >= '1' && d <= '7')) {
          unsigned    v {0};
          std::size_t k {i + 1};
          std::size_t n {0};
          while (k < s.size() && n < 3 && s[k] >= '0' && s[k] <= '7') {
            v = (v * 8U) + static_cast<unsigned>(s[k] - '0');
            ++k;
            ++n;
          }
          out += static_cast<char>(v);
          i    = k;
          continue;
        }
        out += '\\';
        out += d;
        i   += 2;
        continue;
      }
      out += s[i];
      ++i;
    }
    return out;
  }

  std::vector<std::string> split_tabs(const std::string& s)
  {
    std::vector<std::string> v;
    std::string              cur;
    for (const char c : s) {
      if (c == '\t') { v.push_back(cur); cur.clear(); }
      else { cur += c; }
    }
    v.push_back(cur);
    return v;
  }

  std::optional<span> parse_expected(const std::string& f)
  {
    if (f == "NOMATCH" || f == "NULL") { return span {-1, -1}; }
    long a {0};
    long b {0};
    if (std::sscanf(f.c_str(), "(%ld,%ld)", &a, &b) == 2) { return span {a, b}; }
    return std::nullopt; // unparseable -> the case is skipped
  }

  struct counts
  {
    long b[7] {}; // b1, b2, b3, b4, delegated, both_reject, std_only
    long basic_cases {0};
    long extended_cases {0};
  };

  span real_span(const std::string& pat, const std::string& text,
                 rc::regex_constants::syntax_option_type gf, bool& routed, bool& threw)
  {
    try {
      const rc::regex re {pat, gf, rc::policy::fallback};
      routed = re.uses_real();
      rc::smatch m;
      if (rc::regex_search(text, m, re)) { return span {m.position(0), m.position(0) + m.length(0)}; }
    }
    catch (...) { threw = true; }
    return span {-1, -1};
  }

  span std_span(const std::string& pat, const std::string& text, std::regex::flag_type sf, bool& threw)
  {
    try {
      const std::regex s {pat, sf};
      std::smatch      m;
      if (std::regex_search(text, m, s)) {
        return span {static_cast<long>(m.position(0)), static_cast<long>(m.position(0) + m.length(0))};
      }
    }
    catch (...) { threw = true; }
    return span {-1, -1};
  }

  // Classify one (pattern, text, expected) under one grammar into a bucket, tallied into \p c.
  void classify(counts& c, int gi, bool icase, const std::string& pat, const std::string& text, span fowler)
  {
    auto gf = (gi == 0) ? rc::regex_constants::basic : rc::regex_constants::extended;
    std::regex::flag_type sf {(gi == 0) ? std::regex::basic : std::regex::extended};
    if (icase) {
      gf = gf | rc::regex_constants::icase;
      sf = sf | std::regex::icase;
    }
    bool rrouted {false};
    bool rthrew {false};
    bool sthrew {false};
    const span rs {real_span(pat, text, gf, rrouted, rthrew)};
    const span ss {std_span(pat, text, sf, sthrew)};
    if (rthrew && sthrew) { c.b[5]++; return; }   // both_reject
    if (rthrew || sthrew) { c.b[6]++; return; }   // std_only_reject (or the rare real-only reject)
    if (!rrouted) { c.b[4]++; return; }           // delegated
    const int bucket {(rs == ss) ? (rs == fowler ? 0 : 1) : (rs == fowler ? 2 : 3)};
    c.b[bucket]++;
    if (bucket == 3) {
      std::printf("  [b4] pat=%s text=%s REAL=(%ld,%ld) std=(%ld,%ld) Fowler=(%ld,%ld)\n",
                  pat.c_str(), text.c_str(), rs.first, rs.second, ss.first, ss.second, fowler.first, fowler.second);
    }
  }

  // Parse one .dat file into (counts) and accumulate into the global tally \p g.
  counts run_file(const std::string& path, counts& g)
  {
    counts        per_file;
    std::ifstream in {path};
    std::string   line;
    std::string   prev_pat;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#' || line.rfind("NOTE", 0) == 0) { continue; }
      const std::vector<std::string> f {split_tabs(line)};
      if (f.size() < 3) { continue; }
      std::string flags {f[0]};
      const auto  colon {flags.rfind(':')};
      if (colon != std::string::npos) { flags = flags.substr(colon + 1); } // strip a :HA#…: annotation
      const bool has_b {flags.find('B') != std::string::npos};
      const bool has_e {flags.find('E') != std::string::npos};
      const bool has_i {flags.find('i') != std::string::npos};
      if (!has_b && !has_e) { continue; }
      std::string              pat {f[1]};
      std::vector<std::string> rest;
      for (std::size_t k = 2; k < f.size(); ++k) {
        if (!f[k].empty()) { rest.push_back(f[k]); }
      }
      if (rest.empty()) { continue; }
      if (pat == "SAME") { pat = prev_pat; }
      else { prev_pat = pat; }
      if (rest[0] == "NIL") { continue; }
      const std::string raw_text {rest[0] == "NULL" ? std::string {} : rest[0]};
      const std::optional<span> exp {parse_expected(rest.size() >= 2 ? rest[1] : rest[0])};
      if (!exp) { continue; }
      const std::string decoded_pat {decode(pat, true)};
      const std::string decoded_text {decode(raw_text, false)};
      if (has_b) {
        per_file.basic_cases++;
        classify(per_file, 0, has_i, decoded_pat, decoded_text, *exp);
      }
      if (has_e) {
        per_file.extended_cases++;
        classify(per_file, 1, has_i, decoded_pat, decoded_text, *exp);
      }
    }
    for (int k = 0; k < 7; ++k) { g.b[k] += per_file.b[k]; }
    g.basic_cases    += per_file.basic_cases;
    g.extended_cases += per_file.extended_cases;
    return per_file;
  }

  bool expect_eq(const char* what, long got, long want)
  {
    if (got != want) {
      std::printf("FAIL: %s = %ld, expected %ld\n", what, got, want);
      return false;
    }
    return true;
  }

} // namespace

int main(int argc, char** argv)
{
  const std::string dir {argc > 1 ? argv[1] : "tests/corpora/fowler"};
  counts            g;
  // The "no silent caps" pins: the exact per-file parsed-case count per grammar.
  struct file_pin { const char* name; long basic; long extended; };
  const file_pin pins[] {
    {.name = "basic", .basic = 62, .extended = 204},
    {.name = "nullsubexpr", .basic = 8, .extended = 50},
    {.name = "repetition", .basic = 0, .extended = 91},
  };
  bool ok {true};
  for (const file_pin& p : pins) {
    const counts pf {run_file(dir + "/" + p.name + ".dat", g)};
    ok = expect_eq((std::string {p.name} + ".dat basic-cases").c_str(), pf.basic_cases, p.basic) && ok;
    ok = expect_eq((std::string {p.name} + ".dat extended-cases").c_str(), pf.extended_cases, p.extended) && ok;
  }

  const char* bn[] {"b1 REAL==std==Fowler", "b2 REAL==std!=Fowler", "b3 REAL==Fowler!=std",
                    "b4 REAL alone", "delegated", "both_reject", "std_only_reject"};
  std::printf("=== Fowler conformance (compat) ===\n");
  for (int k = 0; k < 7; ++k) { std::printf("  %-24s %ld\n", bn[k], g.b[k]); }
  std::printf("  cases: basic=%ld extended=%ld\n", g.basic_cases, g.extended_cases);

  // The hard invariants (lib-stable): REAL agrees with std on every routed case (b3 == b4 == 0), never
  // over-accepts (std_only == 0), the perfect count is pinned, and the totals are pinned.
  ok = expect_eq("b3 (REAL==Fowler!=std)", g.b[2], 0) && ok;
  ok = expect_eq("b4 (REAL alone)", g.b[3], 0) && ok;
  ok = expect_eq("std_only_reject", g.b[6], 0) && ok;
  ok = expect_eq("b1 perfect count", g.b[0], 64 + 339) && ok;
  ok = expect_eq("total basic cases", g.basic_cases, 70) && ok;
  ok = expect_eq("total extended cases", g.extended_cases, 345) && ok;

  if (ok) { std::printf("fowler-compat: PASS (b3=b4=std_only=0, counts pinned)\n"); return 0; }
  std::printf("fowler-compat: FAIL\n");
  return 1;
}
