// The one-pass arc's foundation: the deterministic UTF-8 trie (OP0.5) and the one-pass builder (OP0).
// Three gates here: the canonical Brüggemann-Klein/RE2 fixtures classify as the theory says; the trie
// accepts a code point's bytes iff the code-point predicate does, over EVERY scalar (not a sample); and an
// independent thread-counter confirms that every pattern the builder calls one-pass really does admit at
// most one thread across any byte (the builder's own teeth, not just the runtime's).
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"
#include "real/lazy_dfa.hpp"
#include "real/onepass.hpp"

using real::detail::build_byte_program;
using real::detail::build_utf8_trie;
using real::detail::byte_program;
using real::detail::dynamic_storage;
using real::detail::onepass;

namespace {

  byte_program byte_prog(const char* pat,
                         real::flags f = real::flags::none)
  {
    const auto st {dynamic_storage::compile(pat, f)};
    return build_byte_program(st.program.view());
  }

  bool is_one_pass(const char* pat,
                   real::flags f = real::flags::none)
  {
    const byte_program bp {byte_prog(pat, f)};
    return bp.eligible && onepass(bp).eligible();
  }
} // namespace

TEST(onepass_canonical_fixtures)
{
  // The Brüggemann-Klein & Wood / RE2 examples. `\d` here is REAL's default Unicode `\d`: thanks to the
  // deterministic trie (OP0.5) it is byte-deterministic, so these agree with RE2's ASCII-`\d` classification
  // — before the trie, Unicode `\d` was never one-pass and this fixture would have needed `flags::ascii`.
  EXPECT(is_one_pass("x*yx*"));          // one-pass
  EXPECT(!is_one_pass("x*x"));           // an x both continues the star and starts the trailing x
  EXPECT(is_one_pass("([^ ]*) (.*)"));   // the space separates deterministically
  EXPECT(!is_one_pass("(.*) (.*)"));     // a space both extends .* and separates
  EXPECT(is_one_pass("(\\d+)-(\\d+)"));
  EXPECT(!is_one_pass("(\\d+).(\\d+)")); // '.' matches a digit too -> ambiguous
  EXPECT(is_one_pass("x(y|z)"));
  EXPECT(!is_one_pass("(xy|xz)"));       // reading x, two branches are live
}

TEST(onepass_our_patterns_measured)
{
  // The flagship is one-pass in DEFAULT (Unicode) mode — the whole point of the trie substrate.
  EXPECT(is_one_pass("(\\w+)@(\\w+)"));
  EXPECT(is_one_pass("([a-z0-9]+)@([a-z0-9]+)"));
  EXPECT(is_one_pass("\\w+"));
  EXPECT(is_one_pass("(\\d+)-(\\d+)-(\\d+)"));
  EXPECT(is_one_pass("[a-z]+"));
  // `_` is itself a `\w`, so it both extends group 1 and starts the separator: a genuine conflict.
  EXPECT(!is_one_pass("(\\w+)_(\\w+)"));
  // more than four user groups exceeds the slot cap -> declined to the Pike VM (not a one-pass failure).
  EXPECT(!is_one_pass("(a)(b)(c)(d)(e)"));
}

TEST(onepass_verbose_bail_reason)
{
  const byte_program bp {byte_prog("(\\w+)_(\\w+)")};
  const onepass      op {bp};
  EXPECT(!op.eligible());
  EXPECT(!op.bail_reason().empty()); // a human-readable reason (which node/class), not a bare bool
}

TEST(utf8_trie_exhaustive_vs_codepoint_predicate)
{
  // OP0.5 gate: for every klass_cp class, the trie accepts a scalar's UTF-8 bytes iff the code-point
  // predicate makes it a member — across all 1,112,064 scalars, surrogates excluded.
  for (const char* pat : {"\\w", "\\d", "\\s", "\\W", "\\D", "\\S"}) {
    const auto                    st {dynamic_storage::compile(pat, real::flags::none)};
    const real::detail::cp_class* cc {nullptr};
    for (const real::detail::instr& in : st.program.code) {
      if (in.op == real::detail::opcode::klass_cp) {
        cc = &st.program.cp_classes[in.arg16];
        break;
      }
    }
    EXPECT(cc != nullptr);
    if (cc == nullptr) {
      continue;
    }
    const auto trie {build_utf8_trie(*cc, st.program.cp_ranges)};

    // A flat byte->child table per node makes the per-scalar walk O(1) per byte.
    std::vector<std::array<std::int32_t, 256>> tbl(trie.nodes.size());
    for (std::size_t i = 0; i < trie.nodes.size(); ++i) {
      tbl[i].fill(-2);
      for (const auto& e : trie.nodes[i].trans) {
        for (unsigned b = e.first.lo; b <= e.first.hi; ++b) {
          tbl[i][b] = e.second;
        }
      }
    }

    std::size_t mismatches {0};
    for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
      if (cp >= 0xD800 && cp <= 0xDFFF) {
        continue; // surrogates are not scalars
      }
      std::uint8_t      bytes[4] {};
      const std::size_t len      {real::detail::encode_utf8_bytes(cp, bytes)};
      bool              accepts  {trie.root >= 0};
      std::int32_t      node     {trie.root};
      for (std::size_t i = 0; i < len && accepts; ++i) {
        const std::int32_t nx {node < 0 ? -2 : tbl[static_cast<std::size_t>(node)][bytes[i]]};
        if (nx == -2) {
          accepts = false;
        }
        else if (i + 1 == len) {
          accepts = (nx == -1); // last byte must land on accept
        }
        else if (nx < 0) {
          accepts = false;      // a code point ended before its bytes did
        }
        else {
          node = nx;
        }
      }
      bool member {false};
      if (cp < 0x80) {
        member = cc->ascii.test(static_cast<std::uint8_t>(cp));
      }
      else {
        for (std::uint32_t k = 0; k < cc->range_count; ++k) {
          const real::detail::code_range& r {st.program.cp_ranges[cc->range_begin + k]};
          if (cp >= r.lo && cp <= r.hi) {
            member = true;
            break;
          }
        }
      }
      if (accepts != member) {
        ++mismatches;
      }
    }
    EXPECT_EQ(mismatches, std::size_t {0});
  }
}

namespace {

  // An independent one-pass check: anchored NFA simulation over the byte-program, returning the greatest
  // number of *consuming* threads that cross any single byte. One-pass <=> this never exceeds 1. This does
  // not consult the builder, so it is a genuine cross-check of the builder's verdict.
  void closure(const byte_program&        bp,
               std::int32_t               pc,
               std::vector<char>&         seen,
               std::vector<std::int32_t>& out)
  {
    if (pc < 0 || static_cast<std::size_t>(pc) >= bp.code.size() || seen[static_cast<std::size_t>(pc)] != 0) {
      return;
    }
    seen[static_cast<std::size_t>(pc)] = 1;
    const real::detail::instr& in {bp.code[static_cast<std::size_t>(pc)]};
    switch (in.op) {
      case real::detail::opcode::split:
        closure(bp, in.primary_target, seen, out);
        closure(bp, in.secondary_target, seen, out);
        break;
      case real::detail::opcode::jump:
        closure(bp, in.primary_target, seen, out);
        break;
      case real::detail::opcode::save:
        closure(bp, pc + 1, seen, out);
        break;
      default:
        out.push_back(pc); // byte / klass / match
        break;
    }
  }

  std::size_t max_crossers(const byte_program& bp,
                           std::string_view    text)
  {
    std::vector<char>         seen(bp.code.size(), 0);
    std::vector<std::int32_t> live;
    closure(bp, 0, seen, live);
    std::size_t worst {0};
    for (const char ch : text) {
      const auto                byte     {static_cast<std::uint8_t>(ch)};
      std::size_t               crossers {0};
      std::vector<std::int32_t> next_seeds;
      for (const std::int32_t pc : live) {
        const real::detail::instr& in      {bp.code[static_cast<std::size_t>(pc)]};
        bool                       matches {false};
        if (in.op == real::detail::opcode::byte) {
          matches = static_cast<std::uint8_t>(in.arg8) == byte;
        }
        else if (in.op == real::detail::opcode::klass) {
          matches = bp.classes[in.arg16].test(byte);
        }
        if (matches) {
          ++crossers;
          next_seeds.push_back(pc + 1);
        }
      }
      worst = std::max(worst, crossers);
      std::ranges::fill(seen, static_cast<char>(0));
      live.clear();
      for (const std::int32_t s : next_seeds) {
        closure(bp, s, seen, live);
      }
    }
    return worst;
  }
} // namespace

TEST(onepass_independent_thread_count_verifier)
{
  // Every pattern the builder calls one-pass must admit <=1 crossing thread on real inputs; a pattern it
  // rejects must show >1 on some input (else the rejection is spurious).
  struct probe
  {
    const char* pat;
    const char* input;
    bool        one_pass;
  };
  const probe probes[] {
    {.pat = "(\\w+)@(\\w+)", .input = "abc@def", .one_pass = true},
    {.pat = "([a-z0-9]+)@([a-z0-9]+)", .input = "a1@b2", .one_pass = true},
    {.pat = "(\\d+)-(\\d+)", .input = "12-34", .one_pass = true},
    {.pat = "x*yx*", .input = "xxyxx", .one_pass = true},
    {.pat = "[a-z]+", .input = "hello", .one_pass = true},
    {.pat = "(\\w+)_(\\w+)", .input = "aa_bb", .one_pass = false},
    {.pat = "(xy|xz)", .input = "xy", .one_pass = false},
    {.pat = "x*x", .input = "xxx", .one_pass = false}};
  for (const probe& p : probes) {
    const byte_program bp       {byte_prog(p.pat)};
    const std::size_t  crossers {max_crossers(bp, p.input)};
    EXPECT_EQ(onepass(bp).eligible(), p.one_pass);
    if (p.one_pass) {
      EXPECT(crossers <= 1); // the builder's verdict, confirmed independently
    }
    else {
      EXPECT(crossers >= 2); // a real ambiguity backs the rejection
    }
  }
}
