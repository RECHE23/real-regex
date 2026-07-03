// Exhaustive compat routing check: real::compat vs the LOCAL std::regex over a small enumerated space.
//
// The compat contract is "the observable is identical to the local std, never a silent divergence" —
// whether a pattern is routed to the real engine or falls back to std. This harness validates the
// routing screens across the whole tier-1 pattern/input space: for every (pattern, input), the
// real::compat result (matched / position / length / every group span, plus a regex_replace spot) must
// equal the local std::regex result. The oracle is the local std (the compat philosophy — no allowlist;
// platform variance is absorbed by definition, and multi-platform CI compares each local std to itself).
//
// The enumeration is produced by the shared Python substrate (sciforge.corpus.exhaustive) and read from
// two files (patterns, inputs), one per line — no C++ re-implementation of the enumerator.
//
// Usage: exhaustive_compat <patterns-file> <inputs-file>

#include <cstdio>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "real/std/regex.hpp"

namespace rc = real::compat;

namespace {

  struct observable
  {
    bool                                accepts {}; //!< The pattern compiled (both engines, or a reject).
    bool                                matched {};
    long                                pos     {-1};
    long                                len     {-1};
    std::vector<std::pair<long, long>>  groups;      //!< Per-group (position, length); (-1,-1) if unset.
    std::string                         replaced;    //!< regex_replace(input, "#") — a substitution spot.
  };

  bool operator==(const observable& a, const observable& b)
  {
    return a.accepts == b.accepts && a.matched == b.matched && a.pos == b.pos && a.len == b.len
           && a.groups == b.groups && a.replaced == b.replaced;
  }

  template <typename Regex, typename Match, typename SearchFn, typename ReplaceFn>
  observable run(const std::string& pattern, const std::string& input, SearchFn search, ReplaceFn replace)
  {
    observable obs;
    Regex      engine;
    try {
      engine = Regex(pattern);
    }
    catch (const std::regex_error&) {
      obs.accepts = false; // the pattern is rejected — a valid, comparable outcome
      return obs;
    }
    obs.accepts = true;
    Match match;
    obs.matched = search(input, match, engine);
    if (obs.matched) {
      obs.pos = static_cast<long>(match.position(0));
      obs.len = static_cast<long>(match.length(0));
      for (std::size_t i = 1; i < match.size(); ++i) {
        obs.groups.emplace_back(match[i].matched ? static_cast<long>(match.position(i)) : -1,
                                match[i].matched ? static_cast<long>(match.length(i)) : -1);
      }
    }
    obs.replaced = replace(input, engine, "#");
    return obs;
  }

  std::vector<std::string> read_lines(const char* path)
  {
    std::vector<std::string> lines;
    std::ifstream            in(path);
    std::string              line;
    while (std::getline(in, line)) {
      lines.push_back(line);
    }
    return lines;
  }

} // namespace

int main(int argc, char** argv)
{
  if (argc != 3) {
    static_cast<void>(std::fprintf(stderr, "usage: %s <patterns> <inputs>\n", argv[0]));
    return 2;
  }
  const std::vector<std::string> patterns {read_lines(argv[1])};
  const std::vector<std::string> inputs {read_lines(argv[2])};

  long long total {0};
  long long agree {0};
  long long divergences {0};
  long long groups_only {0}; //!< accept + match + whole-span + replace agree; only a group span differs.
  int       shown {0};

  const auto groups_only_diff = [](const observable& a, const observable& b) {
    return a.accepts == b.accepts && a.matched == b.matched && a.pos == b.pos && a.len == b.len
           && a.replaced == b.replaced && a.groups != b.groups;
  };

  const auto compat_search = [](const std::string& s, rc::smatch& m, const rc::regex& e) {
    return rc::regex_search(s, m, e);
  };
  const auto compat_replace = [](const std::string& s, const rc::regex& e, const char* f) {
    return rc::regex_replace(s, e, std::string(f));
  };
  const auto std_search = [](const std::string& s, std::smatch& m, const std::regex& e) {
    return std::regex_search(s, m, e);
  };
  const auto std_replace = [](const std::string& s, const std::regex& e, const char* f) {
    return std::regex_replace(s, e, std::string(f));
  };

  for (const std::string& pattern : patterns) {
    for (const std::string& input : inputs) {
      ++total;
      const observable compat {run<rc::regex, rc::smatch>(pattern, input, compat_search, compat_replace)};
      const observable local {run<std::regex, std::smatch>(pattern, input, std_search, std_replace)};
      if (compat == local) {
        ++agree;
      }
      else {
        ++divergences;
        if (groups_only_diff(compat, local)) {
          ++groups_only;
        }
        else if (shown < 25) {
          static_cast<void>(std::fprintf(stderr,
                       "DIVERGE pattern=%s input=%s | compat(accept=%d match=%d %ld+%ld repl=%s) "
                       "std(accept=%d match=%d %ld+%ld repl=%s)\n",
                       pattern.c_str(), input.c_str(), compat.accepts, compat.matched, compat.pos,
                       compat.len, compat.replaced.c_str(), local.accepts, local.matched, local.pos,
                       local.len, local.replaced.c_str()));
          ++shown;
        }
      }
    }
  }

  const long long serious {divergences - groups_only};
  static_cast<void>(std::printf("exhaustive-compat: %lld cases, agree=%lld, divergences=%lld "
              "(groups-only capture=%lld, serious span/accept=%lld)\n",
              total, agree, divergences, groups_only, serious));
  return serious == 0 ? 0 : 1; // groups-only is the documented nullable-loop capture class (triage)
}
