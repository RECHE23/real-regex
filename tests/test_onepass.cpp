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

  byte_program tier_b_prog(const char* pat,
                           real::flags f = real::flags::none)
  {
    const auto st {dynamic_storage::compile(pat, f)};
    return build_byte_program(st.program.view(), /*keep_assertions=*/ true);
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

TEST(onepass_extract_slots_identical_to_pike)
{
  // OP1, population A (one-pass, no assertions — the spans the L2c router produces). For every Pike match
  // [s, e] the one-pass single pass over the FULL text must give byte-identical group slots. (Population B —
  // patterns carrying assertions like \b or ^…$ — is Tier B / OP2b: the byte-program the builder uses is
  // assert-free, so the table cannot carry an assertion yet.)
  const char* pats[] {
    R"((\w+)@(\w+))", R"((\d+)-(\d+))", R"(([a-z]+):([0-9]+))", R"(\w+)", R"(([a-z0-9]+)@([a-z0-9]+))",
    R"((\d+)-(\d+)-(\d+))", R"([A-Za-z]+)", R"((\w+) (\w+))", R"(x*yx*)", R"(([^ ]+))", R"((\w)(\w)(\w))"};
  const char* texts[] {
    "", "abc@def x9@y0", "12-34-56 and 7-8-9", "caf\xC3\xA9@r\xC3\xA9sum\xC3\xA9 ok", "  spaced   words  ",
    "port:8080 host:443", "aaa bbb ccc", "xxyxx zz xyx", "one two three", "a@b"};
  std::size_t checked {0};
  for (const char* pat : pats) {
    const auto    st {dynamic_storage::compile(pat, real::flags::none)};
    const auto    bp {build_byte_program(st.program.view())};
    const onepass op {bp};
    if (!op.eligible()) {
      continue;
    }
    const real::regex rx {pat};
    for (const char* t : texts) {
      const std::string text {t};
      for (const auto& m : rx.find_iter(text)) {
        std::vector<std::size_t> slots;
        EXPECT(op.extract(text, m.start(), m.end(), slots));
        for (std::size_t g = 0; g < m.size(); ++g) {
          const std::size_t ps {m[g].data() != nullptr ? m.start(g) : real::npos};
          const std::size_t pe {m[g].data() != nullptr ? m.end(g) : real::npos};
          EXPECT_EQ(slots[2 * g], ps);
          EXPECT_EQ(slots[(2 * g) + 1], pe);
        }
        ++checked;
      }
    }
  }
  EXPECT(checked >= 30U);
}

#include <atomic>
#include <thread>

TEST(onepass_per_regex_cache_is_thread_safe)
{
  // The per-regex immutable cache (byte-program + one-pass table) is built once under std::call_once. Many
  // threads sharing ONE const regex — the binding releases the GIL and shares the compiled object — must all
  // get correct results with no race on the build. Stress: N threads each run find_iter repeatedly on the
  // same instance; every run must see the same match count. (ASan/UBSan run this; TSan/valgrind on the
  // devbox is the dedicated race detector.)
  const real::regex rx {"(\\w+)@(\\w+)"}; // one-pass; routes; cache built lazily on the first routed search
  std::string       text;
  for (int i = 0; i < 2000; ++i) {
    text += "aa@bb cc@dd ee@ff "; // past the routing threshold, many matches
  }
  std::size_t expected {0};
  for (const auto& m : rx.find_iter(text)) {
    (void) m;
    ++expected;
  }

  constexpr int            n_threads {8};
  constexpr int            n_iters   {50};
  std::atomic<int>         good      {0};
  std::vector<std::thread> threads;
  threads.reserve(n_threads);
  for (int t = 0; t < n_threads; ++t) {
    threads.emplace_back([&] {
                           for (int it = 0; it < n_iters; ++it) {
                             std::size_t n {0};
                             for (const auto& m : rx.find_iter(text)) {
                               (void) m;
                               ++n;
                             }
                             if (n == expected) {
                               good.fetch_add(1, std::memory_order_relaxed);
                             }
                           }
                         });
  }
  for (std::thread& th : threads) {
    th.join();
  }
  EXPECT_EQ(good.load(), n_threads * n_iters); // every run on every thread agreed
}

TEST(onepass_table_memory_cap_declines)
{
  // (ii) The table-memory cap declines a table that would bloat the regex, falling back to the Pike VM. The
  // flagship's minimized table is ~1.8 MB; a 64 KB cap (test hook) forces the decline, and the same pattern
  // under the real ~8 MB cap stays one-pass.
  const byte_program bp {byte_prog("(\\w+)@(\\w+)")};
  EXPECT(onepass(bp).eligible());                             // real cap (~8 MB): one-pass
  const onepass capped  {bp, std::size_t {64} << 10};         // 64 KB cap
  EXPECT(!capped.eligible());
  EXPECT(!capped.bail_reason().empty());                      // a reason, not a bare bool
}

TEST(tier_b_byte_program_keeps_assertions)
{
  // Tier-B (keep_assertions) preserves assert_position ops so the one-pass table can later carry them as
  // edge conditions; Tier-A (default) still declines them, and a bounded lookaround declines in both. The
  // one-pass runtime does not yet consume assertions, so it declines a has_assertions program (the runtime, once it
  // the guard). Foundation slice: the two-population eligibility, before the edge-condition runtime.
  const byte_program anchored {tier_b_prog("^foo$")};
  EXPECT(anchored.eligible);
  EXPECT(anchored.has_assertions);

  const byte_program boundary {tier_b_prog("\\bfoo\\b")};
  EXPECT(boundary.eligible);
  EXPECT(boundary.has_assertions);

  EXPECT(!byte_prog("^foo$").eligible);                    // Tier-A declines a position assertion
  EXPECT(!tier_b_prog("foo(?=bar)").eligible);             // a bounded lookaround declines even in Tier-B

  const byte_program plain {tier_b_prog("(\\w+)@(\\w+)")}; // no assertions: unchanged under keep_assertions
  EXPECT(plain.eligible);
  EXPECT(!plain.has_assertions);

  EXPECT(!real::detail::onepass(anchored).eligible()); // the runtime declines Tier-B until it evaluates edge conditions
}
