//! PCRE2 differential: real::regex against PCRE2, over generated patterns and subjects, arbitrated by `re`.
//!
//! PCRE2 is an independent backtracking engine with the same leftmost-first semantics as `re`, which is
//! REAL's reference. The two engines agree on most of what this generator produces; where they do not, the
//! disagreement is either PCRE2's own semantics (a multiline `^` after a final newline, `\A` under a start
//! offset) or a REAL bug. This program cannot tell which: it prints every divergence, with what the generator
//! knows about the pattern, and `fuzz/pcre2_arbiter.py` asks `re` (or `regex` for a variable-width
//! lookbehind `re` refuses).
//!
//! The generator records two facts the arbiter needs, because both are documented differences from `re`
//! that are decided by the pattern's structure, not by its answers:
//!
//!   * which groups open inside a lookaround (their captures stay empty: div_lookaround_captures);
//!   * whether a repeat that can iterate more than once has a body that can match empty (the nullable-loop
//!     family, where REAL follows RE2 and not `re`).
//!
//! Every position of each subject is compared under `search`, `match` and `fullmatch`, every group of the
//! answer. A position is also checked against `left_context()`: matching from that position in a suffix
//! that keeps `left_context()` bytes before it answers as the whole subject does.
//!
//! Output, one line each, tab-separated:
//!   D what flags ucp pos pattern-hex subject-hex real pcre nullable-loop lookaround-groups
//!   L flags pos pattern-hex subject-hex                       (a left_context answer that differs)
//!   S key=value ...                                           (the summary, last)
//! Spans print as `start,end` per group, `-` for an unset group, `;` between groups, or `nomatch`.
//!
//! With PCRE2_DIFF_CANFAIL set, REAL's first answer that is not empty is moved one byte on every pattern
//! without a nullable loop, so the arbiter has wrong answers to refuse.
//!
//! Usage: pcre2_differential <seed> <patterns> <mode>   mode 0: ASCII, 1: multiline subjects, 2: Unicode icase
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <real/real.hpp>

namespace {

  enum class mode_kind
  {
    ascii,
    multiline,
    unicode_icase,
  };

  struct piece
  {
    std::string text;
    bool        nullable;
  };

  class generator
  {
  public:

    generator(unsigned  seed,
              mode_kind mode) : rng_ {seed}, mode_ {mode}
    {}

    int below(int n)
    {
      return static_cast<int>(rng_() % static_cast<unsigned>(n));
    }

    //! A pattern, with the facts the arbiter needs recorded on the generator.
    std::string pattern()
    {
      group_in_lookaround_.assign(1, false);
      nullable_loop_ = false;
      return alternation(0, false).text;
    }

    bool nullable_loop() const
    {
      return nullable_loop_;
    }

    //! The groups (by number) that open inside a lookaround, comma-separated.
    std::string lookaround_groups() const
    {
      std::string out;
      for (std::size_t g {1}; g < group_in_lookaround_.size(); ++g) {
        if (group_in_lookaround_[g]) {
          out += (out.empty() ? "" : ",") + std::to_string(g);
        }
      }
      return out.empty() ? "-" : out;
    }

    std::string subject()
    {
      static constexpr std::array<std::string_view, 12> unicode {"\xc3\xa9", "\xc3\x89", "k", "K", "\xe2\x84\xaa", "s",
                                                                 "S", "\xc5\xbf", "\xc3\x9f", "ss", " ", "\n"};
      static constexpr std::array<char, 7>              ascii   {'a', 'b', 'x', 'a', 'b', '\n', ' '};
      std::string                                       s;
      for (int n {below(9)}; n > 0; --n) {
        if (mode_ == mode_kind::unicode_icase) {
          s += unicode.at(static_cast<std::size_t>(below(static_cast<int>(unicode.size()))));
        }
        else {
          s += ascii.at(static_cast<std::size_t>(below(mode_ == mode_kind::multiline ? 7 : 5)));
        }
      }
      return s;
    }

  private:

    // A lookaround body must be bounded, so inside one only the quantifiers with a finite maximum are drawn.
    std::string quantifier(bool in_lookaround)
    {
      static constexpr std::array<std::string_view, 12> all     {"", "", "", "+", "*", "?", "{1,2}", "{2}", "+?", "*?", "??", "{0,2}"};
      static constexpr std::array<std::string_view, 8>  bounded {"", "", "", "?", "{1,2}", "{2}", "??", "{0,2}"};
      return std::string {in_lookaround ? bounded.at(static_cast<std::size_t>(below(8))) : all.at(static_cast<std::size_t>(below(12)))};
    }

    // The quantified piece: nullable when its minimum is zero; a nullable body under a repeat that can
    // iterate twice is the nullable-loop family.
    piece quantified(piece base,
                     bool  in_lookaround)
    {
      const std::string q {quantifier(in_lookaround)};
      if (q.empty()) {
        return base;
      }
      const bool repeats  {q != "?" && q != "??"};
      const bool zero_min {q[0] == '*' || q[0] == '?' || q.starts_with("{0")};
      if (repeats && base.nullable) {
        nullable_loop_ = true;
      }
      return {base.text + q, base.nullable || zero_min};
    }

    piece sequence(int  depth,
                   bool in_lookaround)
    {
      piece out {"", true};
      for (int n {1 + below(3)}; n > 0; --n) {
        const piece p {atom(depth, in_lookaround)};
        out.text     += p.text;
        out.nullable  = out.nullable && p.nullable;
      }
      return out;
    }

    piece alternation(int  depth,
                      bool in_lookaround)
    {
      piece out {sequence(depth, in_lookaround)};
      if (below(3) == 0) {
        const piece other {sequence(depth, in_lookaround)};
        out.text     += "|" + other.text;
        out.nullable  = out.nullable || other.nullable;
      }
      return out;
    }

    piece literal(bool in_lookaround)
    {
      static constexpr std::array<std::string_view, 7> ascii   {"a", "b", "x", "ab", "ba", "aa", "\\n"};
      static constexpr std::array<std::string_view, 9> unicode {"\xc3\xa9", "\xc3\x89", "k", "K", "\xe2\x84\xaa", "s", "\xc5\xbf", "\xc3\x9f", "ss"};
      const std::string                                text    {mode_ == mode_kind::unicode_icase ? unicode.at(static_cast<std::size_t>(below(9)))
                                                                                               : ascii.at(static_cast<std::size_t>(below(7)))};
      // One character takes a quantifier; a longer literal would quantify only its last.
      if (text.size() == 1) {
        return quantified({text, false}, in_lookaround);
      }
      return {text, false};
    }

    piece group(int         depth,
                bool        in_lookaround,
                std::string open)
    {
      if (open == "(") {
        group_in_lookaround_.push_back(in_lookaround);
      }
      const piece body {alternation(depth + 1, in_lookaround)};
      return quantified({open + body.text + ")", body.nullable}, in_lookaround);
    }

    piece atom(int  depth,
               bool in_lookaround)
    {
      switch (below(depth > 1 ? 14 : 22)) {
        case 0:
        case 1:
        case 2:  return literal(in_lookaround);
        case 3:  return quantified({"[ab]", false}, in_lookaround);
        case 4:  return quantified({"[^a]", false}, in_lookaround);
        case 5:  return quantified({".", false}, in_lookaround);
        case 6:  return quantified({"\\w", false}, in_lookaround);
        case 7:  return {"\\b", true};
        case 8:  return {"\\B", true};
        case 9:  return {"^", true};
        case 10: return {"$", true};
        case 11: return below(2) != 0 ? piece {"\\A", true} : quantified({"\\s", false}, in_lookaround);
        case 12: return quantified({"x", false}, in_lookaround);
        case 13: return quantified({"a", false}, in_lookaround);
        case 14:
        case 15: return group(depth, in_lookaround, "(");
        case 16: return group(depth, in_lookaround, "(?:");
        default: {
            if (in_lookaround) {
              return group(depth, in_lookaround, "(?:");
            }
            static constexpr std::array<std::string_view, 4> opens {"(?=", "(?!", "(?<=", "(?<!"};
            const std::string                                open  {opens.at(static_cast<std::size_t>(below(4)))};
            const piece                                      body  {alternation(depth + 1, true)};
            return {open + body.text + ")", true};
          }
      }
    }

    std::mt19937      rng_;
    mode_kind         mode_;
    std::vector<bool> group_in_lookaround_;
    bool              nullable_loop_ {false};
  };

  std::string hex(std::string_view s)
  {
    static constexpr std::string_view digits {"0123456789abcdef"};
    std::string                       out;
    for (const char c : s) {
      const auto byte {static_cast<unsigned char>(c)};
      out += digits[byte >> 4U];
      out += digits[byte & 0xFU];
    }
    return out.empty() ? "-" : out;
  }

  using answer = std::optional<std::vector<std::size_t>>; // nullopt: no match; else start, end per group

  std::string show(const answer& a)
  {
    if (!a) {
      return "nomatch";
    }
    std::string out;
    for (std::size_t i {0}; i < a->size(); i += 2) {
      out += i == 0 ? "" : ";";
      out += (*a)[i] == real::npos ? std::string {"-"} : std::to_string((*a)[i]) + "," + std::to_string((*a)[i + 1]);
    }
    return out;
  }

  // PCRE2 backtracks: on some patterns it reaches its match limit and has no answer to compare.
  struct pcre_gave_up
  {};

  answer pcre_answer(pcre2_code*        code,
                     pcre2_match_data*  data,
                     const std::string& s,
                     std::size_t        pos,
                     std::uint32_t      options,
                     std::size_t        groups)
  {
    const int rc {pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(s.data()), s.size(), pos, options, data, nullptr)};
    if (rc == PCRE2_ERROR_NOMATCH) {
      return std::nullopt;
    }
    if (rc == PCRE2_ERROR_MATCHLIMIT || rc == PCRE2_ERROR_DEPTHLIMIT || rc == PCRE2_ERROR_HEAPLIMIT) {
      throw pcre_gave_up {};
    }
    if (rc < 0) {
      std::printf("pcre2_match failed (%d)\n", rc);
      std::exit(2);
    }
    const PCRE2_SIZE*        ov {pcre2_get_ovector_pointer(data)};
    std::vector<std::size_t> spans;
    for (std::size_t g {0}; g <= groups; ++g) {
      const bool set {g < static_cast<std::size_t>(rc) && ov[2 * g] != PCRE2_UNSET};
      spans.push_back(set ? ov[2 * g] : real::npos);
      spans.push_back(set ? ov[(2 * g) + 1] : real::npos);
    }
    return spans;
  }

  template <class Match>
  answer real_answer(const Match& m,
                     std::size_t  groups)
  {
    if (!m.matched()) {
      return std::nullopt;
    }
    std::vector<std::size_t> spans;
    for (std::size_t g {0}; g <= groups; ++g) {
      spans.push_back(m.start(g));
      spans.push_back(m.end(g));
    }
    return spans;
  }
} // namespace

int main(int    argc,
         char** argv)
{
  if (argc != 4) {
    std::printf("usage: pcre2_differential <seed> <patterns> <mode 0|1|2>\n");
    return 2;
  }
  const auto                           seed             {static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10))};
  const long                           count            {std::strtol(argv[2], nullptr, 10)};
  const auto                           mode             {static_cast<mode_kind>(std::strtol(argv[3], nullptr, 10))};
  const bool                           canfail          {std::getenv("PCRE2_DIFF_CANFAIL") != nullptr};
  generator                            gen              {seed, mode};
  long                                 rejected_by_real {0};
  long                                 rejected_by_pcre {0};
  long                                 positions        {0};
  long                                 divergences      {0};
  long                                 left_context_bad {0};
  long                                 pcre_limit       {0};
  long                                 nullable_loops   {0};
  constexpr std::array<const char*, 3> what             {"search", "match", "fullmatch"};
  for (long n {0}; n < count; ++n) {
    const std::string pattern   {gen.pattern()};
    int               flag_bits {gen.below(8)};
    if (mode == mode_kind::multiline) {
      flag_bits |= 2;
    }
    if (mode == mode_kind::unicode_icase) {
      flag_bits |= 1;
    }
    real::flags   rf {real::flags::none};
    std::uint32_t pf {PCRE2_UTF};
    if ((flag_bits & 1) != 0) {
      rf  = rf | real::flags::icase;
      pf |= PCRE2_CASELESS;
    }
    if ((flag_bits & 2) != 0) {
      rf  = rf | real::flags::multiline;
      pf |= PCRE2_MULTILINE;
    }
    if ((flag_bits & 4) != 0) {
      rf  = rf | real::flags::dotall;
      pf |= PCRE2_DOTALL;
    }
    const bool ucp {mode == mode_kind::unicode_icase};
    if (ucp) {
      pf |= PCRE2_UCP;
    }
    int                        error_code   {0};
    PCRE2_SIZE                 error_offset {0};
    pcre2_code*                code         {pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(), pf, &error_code, &error_offset, nullptr)};
    std::optional<real::regex> re;
    try {
      re.emplace(pattern, rf);
    }
    catch (const real::regex_error&) {
      ++rejected_by_real;
      pcre2_code_free(code);
      continue;
    }
    if (code == nullptr) {
      // REAL accepts what PCRE2 refuses: a superset, reported, not compared.
      ++rejected_by_pcre;
      continue;
    }
    nullable_loops += gen.nullable_loop() ? 1 : 0;
    std::uint32_t captures  {0};
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &captures);
    pcre2_match_data* data  {pcre2_match_data_create_from_pattern(code, nullptr)};
    bool              moved {false};
    for (int t {0}; t < 12; ++t) {
      const std::string s {gen.subject()};
      for (std::size_t pos {0}; pos <= s.size(); ++pos) {
        if (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0U) == 0x80U) {
          continue; // inside a code point: not a position either engine starts at in UTF mode
        }
        ++positions;
        constexpr std::array<std::uint32_t, 3> options {0U, PCRE2_ANCHORED, PCRE2_ANCHORED | PCRE2_ENDANCHORED};
        for (std::size_t w {0}; w < 3; ++w) {
          answer mine {real_answer(w == 0 ? re->search(s, pos) : w == 1 ? re->match(s, pos) : re->fullmatch(s, pos), captures)};
          answer theirs;
          try {
            theirs = pcre_answer(code, data, s, pos, options.at(w), captures);
          }
          catch (const pcre_gave_up&) {
            ++pcre_limit;
            continue;
          }
          if (canfail && !moved && !gen.nullable_loop() && mine && (*mine)[1] > (*mine)[0]) {
            --(*mine)[1];
            moved = true;
          }
          if (mine != theirs) {
            ++divergences;
            std::printf("D\t%s\t%d\t%d\t%zu\t%s\t%s\t%s\t%s\t%d\t%s\n", what.at(w), flag_bits, ucp ? 1 : 0, pos, hex(pattern).c_str(), hex(s).c_str(),
                        show(mine).c_str(), show(theirs).c_str(), gen.nullable_loop() ? 1 : 0, gen.lookaround_groups().c_str());
          }
        }
        const std::size_t keep  {std::min(re->left_context(), pos)};
        const std::string tail  {s.substr(pos - keep)};
        const auto        whole {re->match(s, pos)};
        const auto        part  {re->match(tail, keep)};
        if (whole.matched() != part.matched() || (whole.matched() && (whole.start() != part.start() + pos - keep || whole.end() != part.end() + pos - keep))) {
          ++left_context_bad;
          std::printf("L\t%d\t%zu\t%s\t%s\n", flag_bits, pos, hex(pattern).c_str(), hex(s).c_str());
        }
      }
    }
    pcre2_match_data_free(data);
    pcre2_code_free(code);
  }
  std::printf("S\tseed=%u\tmode=%d\tpatterns=%ld\trejected_by_real=%ld\trejected_by_pcre=%ld\tpositions=%ld\tdivergences=%ld\tleft_context_bad=%ld\tpcre_limit=%ld\tnullable_loop_patterns=%ld\n",
              seed, static_cast<int>(mode), count, rejected_by_real, rejected_by_pcre, positions, divergences, left_context_bad, pcre_limit, nullable_loops);
  return 0;
}
