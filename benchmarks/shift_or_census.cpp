// Shift-OR eligibility census -- the measurement that decided NOT to build a bit-parallel engine.
//
// WHAT IT ANSWERS. A bit-parallel (Shift-OR / Glushkov) matcher has the highest ceiling of anything this
// repository has considered: it takes both thread regimes, where the capture-free walk took only the
// single-threaded one. It is also a third engine. So before writing one, count what it could actually take.
//
// COUNT COST, NOT PATTERNS. `route_probe` says 4911 of 7406 COMPOSED patterns reach general_full -- a fact
// about the generator ("any suffix breaks a whole route"), not a budget. This weights by ns/byte over shapes
// that actually cost: the route_cliff ladders, §A rows that reach the general VM, and log / lexer idioms.
//
// FOUR conditions, not three. The first three are the obvious ones; the fourth is what the program hides:
//   1. no user capture   -- `capture_free_walk`. Counting `save` would be wrong: every program has 0 and 1.
//   2. no lookaround     -- `lookarounds` empty.
//   3. <= 64 positions   -- CONSUMING opcodes (byte/klass/klass_cp), not program length. `[a-z]+` and `.*`
//                           are one position; `a|b|c` is three.
//   4. byte automaton    -- Shift-OR steps one BYTE. A text-mode `klass_cp` (`\w`, `\p{...}`, Unicode `.`)
//                           is variable width, so a two-state-looking `\w+.*` is not implementable.
// Word boundaries get their own bucket rather than being folded into "lookaround" -- and `^ $ \A \Z` are
// NOT boundaries: they are edge conditions a scan handles by construction. The first cut lumped every
// `assert_position` together and rejected the trim shape for an anchor, which moved the eligible bucket from
// 3.9 % to 21.2 %. Multiline `^ $` stay in the boundary bucket: they are per-position.
//
// THE VM'S SHARE IS SEPARATED FROM THE DFA'S, which is the column that changes the answer. `thread_hist`
// counts step() calls, so its sum IS the number of positions the general VM actually walked -- no new
// counter was needed. Many general_full rows are lazy-DFA-then-fallback: `ERROR:\s+.*` costs 11 ns/B but
// spends only 10 % of its positions in the VM. Without that column a bit-parallel engine would be credited
// with a gain that belongs to the DFA.
//
// THE ANSWER, on arm64, 4096-byte subjects (see the table the program prints for the per-row detail):
//
//     eligible for Shift-OR       23.6 %   of the general-VM ns/byte in this panel
//     ineligible: user captures   36.2 %
//     ineligible: la / cp / \b    40.2 %
//
// So roughly three quarters is out of reach, and a month of work would address a quarter. That is the
// decision: Shift-OR is not the next train.
//
// THOSE SHARES MOVED ONCE ALREADY, AND THE REASON IS THE POINT. The first run read 21.2 / 40.9 / 37.9. The
// open question at the foot of this comment was then answered -- `count_matches` now walks matching-only
// (real.hpp's count_walk) -- and the two capturing rows got cheaper: `(foo|bar)+baz` 25 -> 20.6 ns/B, the
// email row 23 -> 17.5, both with their VM step counts unchanged. The captures bucket shrank because that
// cost was COLLECTED, not because it was reclassified. Read the split as a budget for a bit-parallel engine
// and it is smaller than it was; read it as an account of where the general VM's time goes and one of its
// three parts was just spent.
//
// TWO THINGS THIS PANEL DOES NOT SAY, stated because the numbers invite both.
//
// It is not the repository's ceiling. It is the sum of ns/byte over eight general_full rows, one vote each.
// A different panel weights differently.
//
// And "ineligible: captures" is the FIRST reason, not the only one. `\b(\w+)@(\w+)\.(com|org)\b` also
// carries `\w` (klass_cp) and `\b`; ignoring its groups would not free it. In this panel the only expensive
// row closed SOLELY by captures is `(foo|bar)+baz` -- about a fifth of the total, not two fifths.
//
// THE OPEN QUESTION IT RAISED IS CLOSED, and the rows above are already the post-answer numbers. This panel
// measures `count_matches`, which reads no group, so the copy-on-write those two rows paid was for captures
// nobody asked for: the capture-free walk skipped them because the PROGRAM has saves past slot 1, not
// because the CALLER wanted them. That is a walk policy rather than a language property, and it is now the
// caller's to set -- see real.hpp's count_walk. The rows stay ineligible here regardless: what changed is
// what they cost, not whether Shift-OR could take them.
//
// BUILD: needs -DREAL_PROFILE for the route and thread counters --
//     c++ -std=c++20 -O2 -DREAL_PROFILE -I include -I benchmarks benchmarks/shift_or_census.cpp
// or `make bench-census`. Informational, never a gate: the ns/byte column is host noise, the four bits and
// the step counts are exact.
#include <real/real.hpp>

#include "bench_warmup.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace prof = real::detail::prof;

namespace {

  volatile std::size_t sink {0};

  template <typename Fn>
  double ns_per_call(Fn&& fn, int samples)
  {
    int inner {1};
    for (; inner < (1 << 20); inner *= 2) {
      const auto t0 {std::chrono::steady_clock::now()};
      for (int k = 0; k < inner; ++k) {
        sink = fn();
      }
      if (std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() >= 50.0) {
        break;
      }
    }
    std::vector<double> d;
    for (int s = 0; s < samples; ++s) {
      const auto t0 {std::chrono::steady_clock::now()};
      for (int k = 0; k < inner; ++k) {
        sink = fn();
      }
      d.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count()
                  / inner);
    }
    std::sort(d.begin(), d.end());
    return d[d.size() / 2];
  }

  std::string grown(const char* unit, std::size_t n)
  {
    std::string s;
    while (s.size() < n) {
      s += unit;
    }
    s.resize(n);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) {
      s.pop_back();
    }
    return s;
  }

  struct verdict
  {
    bool        cf {};          //!< no user capture
    bool        no_la {};       //!< no lookaround
    std::size_t positions {};   //!< consuming opcodes
    bool        byte_only {};   //!< no text-mode klass_cp
    bool        has_wb {};      //!< \b / \B present (own bucket)
  };

  verdict classify(const real::regex& re)
  {
    const auto  prog {re.raw_program()};
    verdict     v;
    v.cf    = prog.hints.capture_free_walk;
    v.no_la = prog.lookarounds.empty();
    bool cp {false};
    bool wb {false};
    for (const auto& in : prog.code) {
      switch (in.op) {
        case real::detail::opcode::byte:
        case real::detail::opcode::klass: ++v.positions; break;
        case real::detail::opcode::klass_cp:
          ++v.positions;
          cp = true;
          break;
        case real::detail::opcode::assert_position:
          {
            // NOT every assertion disqualifies. `^ $ \A \Z` are edge conditions a bit-parallel scan
            // handles by construction (where the window starts and ends). Only the WORD-BOUNDARY family is
            // a per-position predicate over the neighbouring bytes, which Shift-OR has no state for. The
            // first cut lumped them together and rejected the trim shape -- 15.5 ns/B, half its positions
            // in the VM -- for an anchor.
            const auto kind {static_cast<real::detail::assert_kind>(in.arg8)};
            wb = wb || kind == real::detail::assert_kind::word_boundary
                 || kind == real::detail::assert_kind::not_word_boundary
                 || kind == real::detail::assert_kind::word_start
                 || kind == real::detail::assert_kind::word_end
                 || kind == real::detail::assert_kind::line_start   // multiline: per-position, not an edge
                 || kind == real::detail::assert_kind::line_end;
          }
          break;
        default: break;
      }
    }
    v.byte_only = prog.byte_mode || !cp;
    v.has_wb    = wb;
    return v;
  }

  struct row
  {
    const char* pattern;
    const char* unit;
  };

}  // namespace

int main(int argc, char** argv)
{
  const int             samples {argc > 1 ? std::atoi(argv[1]) : 11};
  constexpr std::size_t k_size {4096};
  bench::ramp_and_pin();

  // Shapes that actually cost: the route_cliff ladders, §A rows that reach the general VM, and the log /
  // lexer idioms the earlier sessions measured. Not a compose() draw.
  const row rows[] {
    {R"(^[\t \n\r]+|[\t \n\r]+$)", "   \t indented value here \n  "},
    {R"(ERROR:\s+.*)", "log ERROR:  disk full\n"},
    {R"(ERROR:\s+[^\n]*)", "log ERROR:  disk full\n"},
    {R"((?:ERROR|WARN|FATAL):\s+.*)", "log ERROR:  disk full\n"},
    {R"((foo|bar)+baz)", "xx foobarbaz yy "},
    {R"(\b(\w+)@(\w+)\.(com|org)\b)", "mail rene@example.com now "},
    {R"(^\s*#|//.*$)", "   # a comment line here\n"},
    {R"(^(GET|POST|PUT) /\S+)", "GET /index.html HTTP/1.1\n"},
    {R"(^-{2}[a-z-]+=|^-[a-z]$)", "--log-level=debug\n"},
    {R"([a-z]+@[a-z]+)", "mail rene@example now "},
    {R"(\w+)", "some words here and there "},
    {R"([a-z]+)", "some words here and there "},
    {R"(a|b|c)", "xxaxxbxxcxx "},
    {R"((?:ab)+c)", "xx ababc yy "},
    {R"(\bfoo\b)", "xx foo yy "},
  };

  std::printf("# Shift-OR eligibility census -- %zu-byte subjects, median of %d batched draws\n", k_size,
              samples);
  std::printf("# VM steps = sum(thread_hist) = positions the general VM actually walked\n\n");
  std::printf("  %-30s %8s %7s %6s %5s %4s %4s %3s  %s\n", "pattern", "ns/byte", "matches", "VMstep",
              "VM%%", "cap", "la", "pos", "verdict");

  double total_ns {0.0};
  double eligible_ns {0.0};
  double groups_ns {0.0};
  double other_ns {0.0};

  for (const row& r : rows) {
    const real::regex      re {r.pattern};
    const std::string      s {grown(r.unit, k_size)};
    const std::string_view v {s};
    const verdict          cl {classify(re)};

    prof::reset();
    const std::size_t hits {re.count_matches(v)};
    const auto&       c {prof::snapshot()};
    std::uint64_t     steps {0};
    for (std::size_t i = 0; i < 8; ++i) {
      steps += c.thread_hist[i];
    }
    const bool general {c.routes[static_cast<std::size_t>(prof::route::general_full)] != 0U};

    const double ns  {ns_per_call([&] { return re.count_matches(v); }, samples)};
    const double nsb {ns / static_cast<double>(k_size)};

    const bool eligible {cl.cf && cl.no_la && cl.positions <= 64U && cl.byte_only && !cl.has_wb};
    const char* why {eligible ? "ELIGIBLE"
                              : (!cl.cf ? "no: groups"
                                        : (!cl.no_la ? "no: lookaround"
                                                     : (!cl.byte_only ? "no: klass_cp"
                                                                      : (cl.has_wb ? "no: \\b" : "no: >64"))))};
    if (general) {
      total_ns += nsb;
      if (eligible) {
        eligible_ns += nsb;
      }
      else if (!cl.cf) {
        groups_ns += nsb;
      }
      else {
        other_ns += nsb;
      }
    }

    std::printf("  %-30s %8.2f %7zu %6llu %4.0f%% %4s %4s %3zu  %s%s\n", r.pattern, nsb, hits,
                static_cast<unsigned long long>(steps),
                100.0 * static_cast<double>(steps) / static_cast<double>(k_size), cl.cf ? "no" : "YES",
                cl.no_la ? "no" : "YES", cl.positions, why, general ? "" : "  (not general_full)");
  }

  std::printf("\n  general-VM cost split, as a share of ns/byte (not of pattern count):\n");
  if (total_ns > 0.0) {
    std::printf("    eligible for Shift-OR      %5.1f %%\n", 100.0 * eligible_ns / total_ns);
    std::printf("    ineligible: user captures  %5.1f %%\n", 100.0 * groups_ns / total_ns);
    std::printf("    ineligible: la / cp / \\b   %5.1f %%\n", 100.0 * other_ns / total_ns);
  }
  return 0;
}
