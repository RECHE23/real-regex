// Differential harness: real::compat::re2 (drop-in) vs true libre2 (oracle).
//
// D0 proof (hardening #2): curated cases + can-fail. Not yet a CI libFuzzer job
// (that is D1, after René GO). Compile requires locally-installed re2:
//
//   c++ -std=c++20 -O1 -g -Iinclude fuzz/fuzz_re2.cpp \
//       $(pkg-config --cflags --libs re2) -o build/fuzz_re2 && ./build/fuzz_re2
//
//   REAL_RE2_DIFF_CANFAIL=1 ./build/fuzz_re2   # must exit non-zero (harness can redden)
//
// Oracle pin (this devbox): Homebrew re2 2025-11-05 → package 11.0.0 (pkg-config),
// UCD ~15.0/15.1 (BENCHMARKS.md empirical bound). REAL ships UCD 16.0.0.
//
// Allowlist discipline (non-negotiable): only proven engine differences (Unicode \w /
// UCD vintage, RE2-unsupported syntax REAL accepts). NEVER mask a drop-in parity bug.
// Empty-match GlobalReplace divergences are REAL bugs until proven otherwise.

#include <real/compat/re2/re2.hpp>

#include <re2/re2.h>
#include <re2/set.h>
#include <re2/stringpiece.h> // re2::StringPiece — portable vs absl::string_view (apt RE2 10 vs brew 11)

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace drop = real::compat::re2;

namespace {

  struct Report
  {
    int checked {};
    int bugs {};
    int engine_diffs {};
    int canfail_tripped {};
  };

  void note_bug(Report& r,
                std::string_view id,
                std::string_view detail)
  {
    ++r.bugs;
    std::cout << "BUG  " << id << " — " << detail << "\n";
  }

  void note_engine(Report& r,
                   std::string_view id,
                   std::string_view detail)
  {
    ++r.engine_diffs;
    std::cout << "ENG  " << id << " — " << detail << "\n";
  }

  void note_ok(Report& r,
               std::string_view id)
  {
    ++r.checked;
    std::cout << "ok   " << id << "\n";
  }

  // Patterns REAL accepts as a superset and true RE2 rejects → not a drop-in parity bug
  // (drop-in ok() may still be true because it wraps real::regex). Comparing match results
  // is only meaningful when both compile.
  bool both_ok(const drop::RE2& d,
               const ::RE2&      t)
  {
    return d.ok() && t.ok();
  }

  // Known drop-in syntax gaps: RE2-valid constructs real::compat::re2 currently REJECTS. Each is a
  // subset-direction violation of re2.hpp's "compiles everything RE2 compiles" superset claim — real
  // debt, tracked here (printed KNOWN-GAP, never masked) instead of absorbed into the green engine-
  // diff bucket. Measured 2026-07-17 against libre2 11.0.0 (see .recovery/re2-parity-measurement.md).
  // Each needs a real::regex frontend fix (per-construct, dialect-gated) or an explicit doc-scoping
  // of the superset claim; when one lands, delete its entry and the harness enforces parity for it
  // from then on. EXACT patterns, not substrings: a substring allowlist would re-mask the very bugs
  // this promotion exists to surface.
  bool is_known_re2_gap(std::string_view pat)
  {
    static constexpr std::string_view gaps[] {
      R"(\x{D800})",         // braced hex \x{...} surrogate — measured 2026-07-17 vs libre2 11.0.0: RE2
                              // accepts the whole U+D800-DFFF range unvalidated, real rejects it
                              // (Python-semantics surrogate rejection, shared with \u/\U/\N — deliberately
                              // NOT relaxed just to close this). \x{10FFFF} (valid scalar) closed instead
                              // and moved to the MUST-accept block below; this surrogate sub-edge is what
                              // stayed open.
      R"((?U)a+)",           // (?U) ungreedy-swap inline flag — real: unknown extension
      R"((?P<n>a)(?P<n>b))", // duplicate group name — RE2 tolerates; debatable, allowlisted pending a call
    };
    for (const auto g : gaps) {
      if (pat == g) {
        return true;
      }
    }
    return false;
  }

  // Classify a compile-time (a)symmetry between the drop-in and true RE2. Returns true ONLY when both
  // compiled (caller may then compare match results). The two asymmetric directions are NOT the same
  // — the old symmetric note_engine collapsed them, which is what let a real parity bug hide:
  //   drop accepts, RE2 rejects  → REAL superset (lookaround/possessive/\Z/…) — on-contract  → ENG.
  //   drop rejects, RE2 accepts  → SUBSET VIOLATION of "compiles everything RE2 compiles" — a drop-in
  //                                parity BUG (note_bug) unless it is a known, tracked gap (KNOWN-GAP).
  // This realigns the harness with its own rule (header, line 15: never mask a drop-in parity bug).
  bool classify_compile_asym(Report&          r,
                             std::string_view pat,
                             bool             dok,
                             bool             tok,
                             bool             suppress_allowlist = false)
  {
    if (dok && tok) {
      return true;
    }
    if (dok && !tok) {
      note_engine(r, "superset",
                  std::string("pat=") + std::string(pat) + " — REAL accepts, RE2 rejects (on-contract)");
      return false;
    }
    if (!dok && tok) {
      if (!suppress_allowlist && is_known_re2_gap(pat)) {
        note_engine(r, "KNOWN-GAP",
                    std::string("pat=") + std::string(pat) +
                      " — RE2 accepts, drop-in rejects (tracked debt; re2-parity-measurement.md)");
      }
      else {
        note_bug(r, "subset-violation",
                 std::string("pat=") + std::string(pat) +
                   " — RE2 compiles it, drop-in does NOT (breaks the RE2 superset promise)");
      }
      return false;
    }
    return false; // both reject — agreement, nothing to report
  }

  void check_partial_full(Report& r,
                          std::string_view pat,
                          std::string_view text)
  {
    const drop::RE2 d(pat);
    ::RE2::Options  to;
    to.set_log_errors(false);
    const ::RE2 t(std::string(pat), to);
    if (!classify_compile_asym(r, pat, d.ok(), t.ok())) {
      return; // superset / known-gap / subset-violation all reported by classify; only both-ok proceeds
    }
    const bool fd {drop::RE2::FullMatch(text, d)};
    const bool ft {::RE2::FullMatch(text, t)};
    const bool pd {drop::RE2::PartialMatch(text, d)};
    const bool pt {::RE2::PartialMatch(text, t)};
    const std::string id {"FM/PM " + std::string(pat) + " / " + std::string(text)};
    if (fd != ft || pd != pt) {
      note_bug(r, id, "Full " + std::to_string(fd) + "/" + std::to_string(ft) + " Partial " +
                        std::to_string(pd) + "/" + std::to_string(pt));
    }
    else {
      note_ok(r, id);
    }
  }

  void check_partial_capture(Report& r,
                             std::string_view pat,
                             std::string_view text,
                             bool            longest)
  {
    drop::RE2::Options dopt;
    dopt.set_longest_match(longest);
    const drop::RE2 d(pat, dopt);
    ::RE2::Options to;
    to.set_log_errors(false);
    to.set_longest_match(longest);
    const ::RE2 t(std::string(pat), to);
    if (!both_ok(d, t)) {
      return;
    }
    std::string gd;
    std::string gt;
    const bool  bd {drop::RE2::PartialMatch(text, d, &gd)};
    const bool  bt {::RE2::PartialMatch(text, t, &gt)};
    const std::string id {std::string("PM-cap") + (longest ? "-long" : "-first") + " " +
                          std::string(pat) + " / " + std::string(text)};
    if (bd != bt || gd != gt) {
      note_bug(r, id, "drop=[" + gd + "] true=[" + gt + "]");
    }
    else {
      note_ok(r, id);
    }
  }

  void check_global_replace(Report& r,
                            std::string_view pat,
                            std::string_view text,
                            std::string_view rewrite,
                            bool             longest)
  {
    drop::RE2::Options dopt;
    dopt.set_longest_match(longest);
    const drop::RE2 d(pat, dopt);
    ::RE2::Options to;
    to.set_log_errors(false);
    to.set_longest_match(longest);
    const ::RE2 t(std::string(pat), to);
    if (!both_ok(d, t)) {
      return;
    }
    std::string sd {text};
    std::string st {text};
    const int   nd {drop::RE2::GlobalReplace(&sd, d, rewrite)};
    const int   nt {::RE2::GlobalReplace(&st, t, rewrite)};
    const std::string id {std::string("GR") + (longest ? "-long" : "") + " " + std::string(pat) +
                          " / " + std::string(text) + " -> " + std::string(rewrite)};
    if (nd != nt || sd != st) {
      // Empty-match / nullable-quantifier iteration is the audit-named risk class.
      note_bug(r, id, "drop n=" + std::to_string(nd) + " [" + sd + "] true n=" +
                        std::to_string(nt) + " [" + st + "]");
    }
    else {
      note_ok(r, id);
    }
  }

  void check_replace(Report& r,
                     std::string_view pat,
                     std::string_view text,
                     std::string_view rewrite)
  {
    const drop::RE2 d(pat);
    ::RE2::Options  to;
    to.set_log_errors(false);
    const ::RE2 t(std::string(pat), to);
    if (!both_ok(d, t)) {
      return;
    }
    std::string sd {text};
    std::string st {text};
    const bool  bd {drop::RE2::Replace(&sd, d, rewrite)};
    const bool  bt {::RE2::Replace(&st, t, rewrite)};
    const std::string id {"R " + std::string(pat) + " / " + std::string(text) + " rw=" +
                          std::string(rewrite)};
    if (bd != bt || sd != st) {
      note_bug(r, id, "drop [" + sd + "] true [" + st + "]");
    }
    else {
      note_ok(r, id);
    }
  }

  void check_quote_meta(Report& r,
                        std::string_view unquoted)
  {
    const std::string dq {drop::RE2::QuoteMeta(unquoted)};
    const std::string tq {::RE2::QuoteMeta(unquoted)};
    const std::string id {"QM size=" + std::to_string(unquoted.size())};
    if (dq != tq) {
      note_bug(r, id, "byte-for-byte QuoteMeta mismatch");
    }
    else {
      note_ok(r, id);
    }
  }

  void check_set(Report& r,
                 drop::RE2::Anchor d_anchor,
                 ::RE2::Anchor     t_anchor,
                 std::string_view  name,
                 const std::vector<std::string>& patterns,
                 const std::vector<std::string>& subjects)
  {
    drop::RE2::Options dopt;
    drop::RE2::Set     dset(dopt, d_anchor);
    ::RE2::Options     topt;
    topt.set_log_errors(false);
    ::RE2::Set tset(topt, t_anchor);
    for (const auto& p : patterns) {
      if (dset.Add(p, nullptr) < 0 || tset.Add(p, nullptr) < 0) {
        note_engine(r, "Set-Add", "pattern rejected asymmetrically: " + p);
        return;
      }
    }
    if (!dset.Compile() || !tset.Compile()) {
      note_bug(r, "Set-Compile-" + std::string(name), "Compile failed on one side");
      return;
    }
    for (const auto& sub : subjects) {
      std::vector<int> hd;
      std::vector<int> ht;
      const bool       bd {dset.Match(sub, &hd)};
      const bool       bt {tset.Match(sub, &ht)};
      // Order of indices may differ; compare as sorted multisets of hits + verdict.
      auto sorted = [](std::vector<int> v) {
        std::sort(v.begin(), v.end());
        return v;
      };
      const std::string id {"Set-" + std::string(name) + " / " + sub};
      if (bd != bt || sorted(hd) != sorted(ht)) {
        note_bug(r, id, "hit-set or verdict mismatch");
      }
      else {
        note_ok(r, id);
      }
    }
  }

  void check_find_and_consume(Report& r,
                              std::string_view pat,
                              std::string_view text)
  {
    const drop::RE2 d(pat);
    ::RE2::Options  to;
    to.set_log_errors(false);
    const ::RE2 t(std::string(pat), to);
    if (!both_ok(d, t)) {
      return;
    }
    std::string_view  idrop {text};
    // re2::StringPiece is the portable input cursor for FindAndConsume across RE2
    // versions (Homebrew 11 exposes absl::string_view; Ubuntu apt 10 may not pull
    // absl into the TU without this typedef header).
    re2::StringPiece  itrue {text.data(), text.size()};
    // Cap iterations so a pathological empty-match loop cannot hang the harness.
    for (int i = 0; i < 64; ++i) {
      std::string gd;
      std::string gt;
      // Prefer a single capture when the pattern has one; else just consume.
      const bool has_cap {pat.find('(') != std::string_view::npos};
      bool       bd {};
      bool       bt {};
      if (has_cap) {
        bd = drop::RE2::FindAndConsume(&idrop, d, &gd);
        bt = ::RE2::FindAndConsume(&itrue, t, &gt);
      }
      else {
        bd = drop::RE2::FindAndConsume(&idrop, d);
        bt = ::RE2::FindAndConsume(&itrue, t);
      }
      if (!bd && !bt) {
        break;
      }
      if (bd != bt || gd != gt) {
        note_bug(r, "FAC " + std::string(pat) + " / " + std::string(text),
                 "stream diverged at step " + std::to_string(i));
        return;
      }
    }
    const std::string rem_d {std::string(idrop)};
    const std::string rem_t {std::string(itrue.data(), itrue.size())};
    if (rem_d != rem_t) {
      note_bug(r, "FAC-rem " + std::string(pat), "remainder drop=[" + rem_d + "] true=[" + rem_t + "]");
    }
    else {
      note_ok(r, "FAC " + std::string(pat) + " / " + std::string(text));
    }
  }

  // Compile-parity probe: exercises ONLY the accept/reject decision (no match comparison), so the
  // promoted subset-direction net has explicit curated inputs. Plain RE2 syntax real must accept,
  // deliberate REAL supersets, and the known gaps all route through classify_compile_asym.
  void check_compile_parity(Report&          r,
                            std::string_view pat)
  {
    const drop::RE2 d(pat);
    ::RE2::Options  to;
    to.set_log_errors(false);
    const ::RE2 t(std::string(pat), to);
    if (classify_compile_asym(r, pat, d.ok(), t.ok())) {
      note_ok(r, std::string("compile-parity ") + std::string(pat));
    }
  }

  // Unicode / class engine differences — allowlisted with proof, never silently.
  void check_engine_word_class(Report& r)
  {
    // BENCHMARKS.md: RE2 \w is ASCII-ish; REAL \w is UTS#18 (UCD 16). café → diverge by design.
    const std::string cafe {"caf\xc3\xa9"};
    const bool        d {drop::RE2::FullMatch(cafe, "\\w+")};
    const bool        t {::RE2::FullMatch(cafe, "\\w+")};
    if (d && !t) {
      note_engine(r, R"(\w+ on UTF-8 letter)",
                  "REAL UCD16 word vs RE2 ~15 ASCII-ish \\w (BENCHMARKS.md) — allowlisted");
    }
    else if (d != t) {
      note_bug(r, R"(\w+ cafe unexpected polarity)", "drop=" + std::to_string(d) + " true=" +
                                                       std::to_string(t));
    }
    else {
      note_ok(r, R"(\w+ cafe — same verdict this RE2 pin)");
    }
  }

  void run_curated(Report& r)
  {
    std::cout << "=== oracle: libre2 pkg " << "11.0.0 (Homebrew re2/2025-11-05), UCD ~15 ===\n";
    std::cout << "=== drop-in: real::compat::re2 @ tip, REAL UCD 16.0.0 ===\n\n";

    // --- FullMatch / PartialMatch (verdict) ---
    check_partial_full(r, "abc", "xabcx");
    check_partial_full(r, "a.c", "abc");
    check_partial_full(r, "a*", "aaa");
    check_partial_full(r, "^a", "ba");
    check_partial_full(r, "a$", "ab");
    check_partial_full(r, R"(\d+)", "12x");
    check_partial_full(r, R"(\bthe\b)", "the");
    check_partial_full(r, R"(\bthe\b)", "then");
    // \Q...\E literal quoting (closed 2026-07-17): match parity, incl. the quantifier-binds-to-the-
    // last-span-character edge (\Qab\E+ == ab+) and the grammar-invisible empty span (a\Q\E+ == a+).
    check_partial_full(r, R"(\Qa.b\E)", "a.b");
    check_partial_full(r, R"(\Qa.b\E)", "axb");
    check_partial_full(r, R"(\Qab\E+)", "abb");
    check_partial_full(r, R"(\Qab\E+)", "abab");
    check_partial_full(r, R"(\Qa.b)", "a.b");
    check_partial_full(r, R"(a\Q\E+)", "aa");
    // (?i-s) global flag removal (closed 2026-07-17): match parity, not just compile — the removal
    // semantics (i enabled, s disabled for the rest of the pattern) must agree with the oracle, not
    // merely "both compile". `.` must NOT cross \n (s off) while A/a fold together (i on).
    check_partial_full(r, R"((?i-s)A.B)", "a\nb");
    check_partial_full(r, R"((?i-s)A.B)", "axb");

    // --- longest-match ties (audit priority) ---
    check_partial_capture(r, "(a|ab)", "xabx", false);
    check_partial_capture(r, "(a|ab)", "xabx", true);
    check_partial_capture(r, "(ab|a)", "xabx", false);
    check_partial_capture(r, "(ab|a)", "xabx", true);
    check_partial_capture(r, "(a+|ab)", "aaab", false);
    check_partial_capture(r, "(a+|ab)", "aaab", true);

    // --- empty-match / nullable GlobalReplace (audit priority) ---
    check_global_replace(r, "a*", "bbb", "#", false);
    check_global_replace(r, "a*", "aa", "#", false);
    check_global_replace(r, "a*", "a", "#", false);
    check_global_replace(r, "a*", "aaa", "#", false);
    check_global_replace(r, "a*", "aab", "#", false);
    check_global_replace(r, "x?", "yyy", "#", false);
    check_global_replace(r, "()", "ab", "#", false);
    check_global_replace(r, "a+", "aa", "#", false);
    check_global_replace(r, "a|ab", "xabx", "#", false);
    check_global_replace(r, "a|ab", "xabx", "#", true);
    check_global_replace(r, R"(\bthe\b)", "the the", "#", false);

    // --- Replace rewrite escapes ---
    check_replace(r, "(a)(b)", "ab", R"(\1)");
    check_replace(r, "(a)(b)", "ab", R"(\0)");
    check_replace(r, "(a)(b)", "ab", R"(\\)");
    check_replace(r, "(a)(b)", "ab", R"(x\1y)");
    check_replace(r, R"((\w)(\d))", "a1 b2", R"(\2\1)");

    // --- QuoteMeta NUL / high-byte (audit priority) ---
    check_quote_meta(r, "a.b*c");
    check_quote_meta(r, std::string("a\0b", 3));
    check_quote_meta(r, "\x80");
    check_quote_meta(r, "caf\xc3\xa9");
    check_quote_meta(r, "a.b*c+?[](){}|^$\\\n");

    // --- Set + anchoring (audit priority) ---
    check_set(r, drop::RE2::Anchor::UNANCHORED, ::RE2::UNANCHORED, "UNANCHORED",
              {"foo", "bar"}, {"foobar", "foo", "bar", "nothing", "xfooy"});
    check_set(r, drop::RE2::Anchor::ANCHOR_START, ::RE2::ANCHOR_START, "START",
              {"foo"}, {"foo", "foobar", "xfoo", "xfooy"});
    check_set(r, drop::RE2::Anchor::ANCHOR_BOTH, ::RE2::ANCHOR_BOTH, "BOTH",
              {"foo"}, {"foo", "foobar", "xfoo"});

    // --- FindAndConsume stream ---
    check_find_and_consume(r, R"((\d+))", "12 34 56");
    check_find_and_consume(r, "a+", "aaabaaa");

    // --- compile parity: the subset-direction net (real must compile everything RE2 compiles) ---
    // Plain RE2 syntax real MUST accept (both-ok; a regression here reddens as a subset-violation):
    check_compile_parity(r, R"(\d+)");
    check_compile_parity(r, "[[:alpha:]]");
    check_compile_parity(r, "(?i)abc");
    check_compile_parity(r, R"(\pL)");
    check_compile_parity(r, R"(\p{L})");
    check_compile_parity(r, R"(\p{Nd})");
    check_compile_parity(r, "(?P<name>a)");
    check_compile_parity(r, R"(\p{Any})");  // meta-property "every code point" — closed 2026-07-17
    check_compile_parity(r, R"(\p{^L})");   // caret-negation == \P{L} — closed 2026-07-17
    check_compile_parity(r, R"(\x{10FFFF})"); // braced hex \x{...} (max valid scalar) — closed 2026-07-17
    check_compile_parity(r, R"(\Qa.b\E)");    // \Q...\E literal quoting — closed 2026-07-17
    check_compile_parity(r, R"((?i-s)a)");    // global flag removal `(?flags-flags)` — closed 2026-07-17
    // Deliberate REAL supersets (drop accepts, RE2 rejects → ENG, on-contract):
    check_compile_parity(r, R"(\Z)");
    check_compile_parity(r, "(?>a)");
    check_compile_parity(r, "a*+");
    // Known RE2 gaps real currently rejects (→ KNOWN-GAP, tracked debt — NOT a fail):
    check_compile_parity(r, R"(\x{D800})"); // braced hex surrogate — RE2 accepts, real rejects (sub-edge)
    check_compile_parity(r, R"((?U)a+)");
    check_compile_parity(r, R"((?P<n>a)(?P<n>b))");

    // --- Engine-diff allowlist (justified) ---
    check_engine_word_class(r);
  }

  // Inject a deliberate false mismatch so a green run is not a mute no-op.
  void run_canfail(Report& r)
  {
    std::cout << "\n=== can-fail inject (must report BUG) ===\n";
    // Force a fake GlobalReplace comparison failure path by comparing drop vs a mutated expect.
    const drop::RE2 d("abc");
    ::RE2::Options  to;
    to.set_log_errors(false);
    const ::RE2 t("abc", to);
    std::string sd {"xabcx"};
    std::string st {"xabcx"};
    (void) drop::RE2::GlobalReplace(&sd, d, "#");
    (void) ::RE2::GlobalReplace(&st, t, "#");
    // Both should be x#x; poison true side in the comparison only.
    st = "POISON";
    if (sd != st) {
      note_bug(r, "can-fail inject", "deliberate mismatch caught (harness can redden)");
      ++r.canfail_tripped;
    }
    else {
      std::cout << "FATAL: can-fail did not trip\n";
    }

    // can-fail #2: a subset-violation with the allowlist SUPPRESSED must become a BUG. Proves the
    // promoted subset direction can redden, and that the allowlist is the only thing sparing the
    // known gaps (remove a gap's entry and this is exactly what the harness would do to it).
    // The inject pattern must be a STILL-OPEN gap (real rejects, RE2 accepts): it was \Qx\E until
    // \Q...\E closed on 2026-07-17 (real now accepts it — the inject would go mute), so it is now
    // the \x{D800} surrogate sub-edge, the longest-lived entry in gaps[] above.
    const int   bugs_before {r.bugs};
    const drop::RE2 dg(R"(\x{D800})"); // real rejects the surrogate; true RE2 accepts it → subset direction
    ::RE2::Options  tgo;
    tgo.set_log_errors(false);
    const ::RE2 tg(std::string(R"(\x{D800})"), tgo);
    classify_compile_asym(r, R"(\x{D800})", dg.ok(), tg.ok(), /*suppress_allowlist=*/true);
    if (r.bugs > bugs_before) {
      std::cout << "can-fail subset-violation: reddened as expected (allowlist suppressed)\n";
      ++r.canfail_tripped;
    }
    else {
      std::cout << "FATAL: subset-violation can-fail did not trip\n";
    }
  }

} // namespace

int main(int argc,
         char** argv)
{
  (void) argc;
  const bool canfail {std::getenv("REAL_RE2_DIFF_CANFAIL") != nullptr};
  // Optional single-case filter later; D0 runs the full curated set.
  (void) argv;

  Report r;
  run_curated(r);
  if (canfail) {
    run_canfail(r);
  }

  std::cout << "\n=== summary ===\n";
  std::cout << "checked_ok=" << r.checked << " bugs=" << r.bugs
            << " engine_diffs=" << r.engine_diffs
            << " canfail_tripped=" << r.canfail_tripped << "\n";

  if (canfail) {
    // Success of the can-fail mode = we detected the inject (exit 0 only if tripped).
    // Also expect at least the real curated bugs; exit non-zero if inject missed.
    return r.canfail_tripped > 0 ? 0 : 2;
  }
  // Normal mode: non-zero if any drop-in parity bug (engine diffs alone are not failure).
  return r.bugs > 0 ? 1 : 0;
}
