// Structural + invariant guard for the generated `\p{Gc}` property tables (unicode_property.hpp). The full
// exhaustive-vs-UCD oracle (all 1.1M scalars vs unicodedata.category) lives in the Python regen guard
// (test_unicode_property_regen) — C++ has no UCD, so here we check what is checkable without one: each table is
// sorted and coalesced, known code points land in the right category, and each top-level group equals the union
// of its categories on a spread of probes. Wired at ast.hpp::resolve_property; the parser-level tests
// live in tests/frontend/test_unicode_property_class.cpp.
#include <cstddef>

#include <sciforge/test/framework.hpp>
#include "real/unicode/unicode_property.hpp"

using namespace real::detail;

TEST(gc_tables_are_sorted_and_coalesced)
{
  for (const std::span<const code_range> ranges : gc_property_ranges) {
    for (std::size_t i = 0; i < ranges.size(); ++i) {
      EXPECT(ranges[i].lo <= ranges[i].hi);
      if (i > 0) {
        EXPECT(ranges[i - 1].hi + 1 < ranges[i].lo); // strictly increasing AND non-adjacent (fully coalesced)
      }
    }
  }
}

TEST(gc_probes_known_code_points)
{
  EXPECT(is_gc_cp(gc_property::Lu, U'A'));
  EXPECT(is_gc_cp(gc_property::Ll, U'a'));
  EXPECT(is_gc_cp(gc_property::Ll, U'é')); // é
  EXPECT(is_gc_cp(gc_property::Lt, U'ǅ')); // Dž (titlecase)
  EXPECT(is_gc_cp(gc_property::Nd, U'3'));
  EXPECT(is_gc_cp(gc_property::Nd, U'٠')); // Arabic-indic zero
  EXPECT(is_gc_cp(gc_property::No, U'⅓')); // vulgar fraction one third
  EXPECT(is_gc_cp(gc_property::Mn, U'֢')); // Hebrew accent
  EXPECT(is_gc_cp(gc_property::Zs, U' '));
  EXPECT(is_gc_cp(gc_property::Pc, U'_'));
  EXPECT(is_gc_cp(gc_property::Ps, U'('));
  EXPECT(is_gc_cp(gc_property::Pe, U')'));
  EXPECT(is_gc_cp(gc_property::Cf, U'‍')); // ZWJ
  EXPECT(is_gc_cp(gc_property::Cc, U'\n'));
  // negatives — a category rejects what belongs to another
  EXPECT(!is_gc_cp(gc_property::Lu, U'a'));
  EXPECT(!is_gc_cp(gc_property::Nd, U'A'));
  EXPECT(!is_gc_cp(gc_property::Ll, U'3'));
}

TEST(gc_groups_equal_union_of_their_categories)
{
  // The invariant that makes L/M/N/P/S/Z/C usable: a group holds a code point iff one of its categories does.
  // Checked on a spread of representative code points (the exhaustive form is the Python regen guard).
  struct probe { char32_t cp; gc_property group; bool in; };
  const probe probes[] {
    {.cp = U'A', .group = gc_property::L, .in = true}, {.cp = U'é', .group = gc_property::L, .in = true},
    {.cp = U'3', .group = gc_property::L, .in = false}, {.cp = U'3', .group = gc_property::N, .in = true},
    {.cp = U'⅓', .group = gc_property::N, .in = true}, {.cp = U'A', .group = gc_property::N, .in = false},
    {.cp = U'֢', .group = gc_property::M, .in = true}, {.cp = U'a', .group = gc_property::M, .in = false},
    {.cp = U'_', .group = gc_property::P, .in = true}, {.cp = U'(', .group = gc_property::P, .in = true},
    {.cp = U'A', .group = gc_property::P, .in = false}, {.cp = U' ', .group = gc_property::Z, .in = true},
    {.cp = U'\n', .group = gc_property::Z, .in = false}, {.cp = U'‍', .group = gc_property::C, .in = true},
    {.cp = U'\n', .group = gc_property::C, .in = true}, {.cp = U'A', .group = gc_property::C, .in = false},
  };
  const gc_property cats[] {
    gc_property::Lu, gc_property::Ll, gc_property::Lt, gc_property::Lm, gc_property::Lo, gc_property::Mn,
    gc_property::Mc, gc_property::Me, gc_property::Nd, gc_property::Nl, gc_property::No, gc_property::Pc,
    gc_property::Pd, gc_property::Ps, gc_property::Pe, gc_property::Pi, gc_property::Pf, gc_property::Po,
    gc_property::Sm, gc_property::Sc, gc_property::Sk, gc_property::So, gc_property::Zs, gc_property::Zl,
    gc_property::Zp, gc_property::Cc, gc_property::Cf, gc_property::Co, gc_property::Cn,
  };
  for (const probe& pr : probes) {
    EXPECT(is_gc_cp(pr.group, pr.cp) == pr.in);
    if (pr.in) {
      bool any {false};
      for (const gc_property c : cats) {
        any = any || is_gc_cp(c, pr.cp);
      }
      EXPECT(any); // a group hit must be backed by some category hit
    }
  }
}
