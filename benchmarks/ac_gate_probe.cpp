// x86-64 leg of the AC gate's second quantity: the THREE regimes it trades between, measured through
// NORMAL dispatch. benchmarks/ac_regime.cpp cannot answer this -- it forces AC on and off to compare
// the two strategies, so the gate's own choice never runs, and the gate's choice is the whole change.
//
// Corpora are specified HERE rather than borrowed: ac_regime's subjects are 4000 bytes and file-local,
// while the arm64 figures this leg checks were taken over 100 KB. Same shape, same branch count, same
// candidate stride; the numbers stand on their own rather than as a byte-for-byte reproduction.
//
//   completing     one branch head per 10 bytes, every one of them a real match
//   false_starts   one branch head per 10 bytes, none of them completing
//   prose          no branch head anywhere -- the gate samples, finds nothing, declines
//
// `make bench-ac-gate` builds and runs it against this tree. To JUDGE a change, build the same source
// against two trees and interleave the runs -- the gate's routing decision is not a row bench_minimal
// carries, so bench-layout-min cannot see this at all:
//   for t in <base> <cand>; do c++ -std=c++20 -O2 -I$t/include benchmarks/ac_gate_probe.cpp -o p_$t; done
//   for r in 1 2 3 4 5; do for t in <base> <cand>; do ./p_$t 15; done; done
// Take the MINIMUM per (tree, regime) across passes and compare that against the per-row spread the
// passes themselves show: on an idle x86-64 host this probe repeats to about +/-7 %, so anything under
// that is not a reading. The first pass of a fresh binary is routinely an outlier (5.03 ns/B on a row
// that then settles at 3.0) -- one more reason the minimum is the statistic, never a single pass.
#include <real/real.hpp>

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <string>
#include <vector>

namespace {

  // Twelve branches: the count at which the arm64 sweep showed the verdict flipping on completion
  // rate alone, and above ac_branch_threshold so the high-region density constant applies.
  const char* const k_words[] {"alpha", "bravo",  "charlie", "delta", "echo", "foxtrot",
                               "golf",  "hotel",  "india",   "juliet", "kilo", "lima"};
  constexpr std::size_t k_branches {12};
  constexpr std::size_t k_size {100000};
  constexpr std::size_t k_stride {10};

  std::string pattern_for()
  {
    std::string p;
    for (std::size_t i = 0; i < k_branches; ++i) {
      if (i != 0) {
        p += '|';
      }
      p += k_words[i];
    }
    return p;
  }

  //! Every candidate completes: a real branch every k_stride bytes, cycling the twelve.
  std::string subject_completing()
  {
    std::string s(k_size, '_');
    for (std::size_t i = 0, w = 0; i + 8 < k_size; i += k_stride, ++w) {
      const std::string word {k_words[w % k_branches]};
      s.replace(i, word.size(), word);
    }
    return s;
  }

  //! No candidate completes: the head byte every k_stride bytes, then a byte no branch continues with.
  std::string subject_false_starts()
  {
    std::string s(k_size, '_');
    for (std::size_t i = 0, w = 0; i + 8 < k_size; i += k_stride, ++w) {
      s[i]     = k_words[w % k_branches][0];
      s[i + 1] = 'Z';  // uppercase: no branch has one, so every candidate dies on its second byte
    }
    return s;
  }

  //! No branch head at all: what ordinary prose looks like to this gate.
  std::string subject_prose()
  {
    std::string s;
    while (s.size() < k_size) {
      s += "zwyx ";
    }
    s.resize(k_size);
    return s;
  }

  double min_ns_per_byte(const real::regex& re, const std::string& s, int reps, std::size_t& count)
  {
    double best {-1.0};
    for (int r = 0; r < reps; ++r) {
      const auto t0 {std::chrono::steady_clock::now()};
      count = re.count_matches(s);
      const auto t1 {std::chrono::steady_clock::now()};
      const double ns {std::chrono::duration<double, std::nano>(t1 - t0).count()
                       / static_cast<double>(s.size())};
      if (best < 0.0 || ns < best) {
        best = ns;
      }
    }
    return best;
  }

}  // namespace

int main(int argc, char** argv)
{
  const int reps {argc > 1 ? std::atoi(argv[1]) : 15};
  const real::regex re {pattern_for()};

  struct row
  {
    const char* name;
    std::string subject;
  };
  const std::vector<row> rows {{"completing", subject_completing()},
                               {"false_starts", subject_false_starts()},
                               {"prose", subject_prose()}};

  std::printf("# ac-gate regimes -- %zu branches, %zu-byte subjects, 1 candidate per %zu bytes, min of %d\n",
              k_branches, k_size, k_stride, reps);
  for (const row& r : rows) {
    std::size_t  n {0};
    const double nspb {min_ns_per_byte(re, r.subject, reps, n)};
    std::printf("%-14s %8.3f ns/B  matches=%zu\n", r.name, nspb, n);
  }
  return 0;
}
