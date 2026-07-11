// Structural + invariant guard for the generated Script_Extensions tables (unicode_scx.hpp). The full
// exhaustive-vs-UCD oracle lives in the Python regen guard (test_unicode_scx_regen, parsed straight from
// the bundled Scripts.txt + ScriptExtensions.txt) -- here we check what is checkable in C++: every
// script's own scx table is sorted and disjoint, known code points land where the UCD says (a multi-scx
// code point proving Script_Extensions is NOT a partition unlike Script, an override that excludes its
// own base script, the default rule for an unoverridden code point). Wired at ast.hpp::resolve_property
// (scx= only, no bare-name form); the parser-level tests live in
// tests/frontend/test_unicode_property_class.cpp.
#include <cstddef>

#include <sciforge/test/framework.hpp>
#include "real/unicode/unicode_scx.hpp"

using namespace real::detail;

TEST(scx_tables_are_sorted_and_disjoint)
{
  // Not a partition (a code point can be in several scripts' scx, or just its own Script by default) --
  // so each script's OWN table must be sorted/disjoint, unlike script_ranges' single whole-code-space
  // invariant. scx_ranges[0] (script::Unknown) is deliberately empty -- skip it, cp_in_ranges handles it.
  for (const std::span<const code_range>& ranges : scx_ranges) {
    for (std::size_t i = 0; i < ranges.size(); ++i) {
      EXPECT(ranges[i].lo <= ranges[i].hi);
      if (i > 0) {
        EXPECT(ranges[i - 1].hi < ranges[i].lo);
      }
    }
  }
}

TEST(scx_probes_known_code_points)
{
  // U+0660 ARABIC-INDIC DIGIT ZERO: base Script=Arabic, scx={Arab, Thaa, Yezi} (ScriptExtensions.txt) --
  // in BOTH Arabic's and Thaana's scx at once, the direct proof scx is not a partition.
  EXPECT(is_scx_cp(script::Arabic, 0x0660));
  EXPECT(is_scx_cp(script::Thaana, 0x0660));
  EXPECT(!is_scx_cp(script::Latin, 0x0660));
  // U+0300 COMBINING GRAVE ACCENT: base Script=Inherited, scx={Cher, Copt, Cyrl, Grek, Latn, Perm, Sunu,
  // Tale} -- the override REPLACES the default, so Inherited (its own base script) is NOT in its own scx.
  EXPECT(!is_scx_cp(script::Inherited, 0x0300));
  EXPECT(is_scx_cp(script::Latin, 0x0300));
  EXPECT(is_scx_cp(script::Greek, 0x0300));
  EXPECT(is_scx_cp(script::Cyrillic, 0x0300));
  EXPECT(!is_scx_cp(script::Han, 0x0300)); // Han is not one of the listed overrides
  // Default rule: an ordinary Latin letter has no override, so scx == {Script(cp)} exactly.
  EXPECT(is_scx_cp(script::Latin, U'A'));
  EXPECT(!is_scx_cp(script::Greek, U'A'));
  EXPECT(!is_scx_cp(script::Inherited, U'A'));
  // Unknown is never a member of any scx (ScriptExtensions.txt never lists Zzzz; scx_ranges[Unknown] is
  // the empty table by construction, see the generator).
  EXPECT(!is_scx_cp(script::Unknown, U'A'));
}

TEST(scx_is_a_superset_of_or_equal_to_script_for_unoverridden_code_points)
{
  // For a handful of ordinary, non-overridden code points across different scripts, scx must agree
  // exactly with Script (the default rule) -- sampled rather than exhaustive (the Python regen guard is
  // exhaustive against the UCD source).
  const struct { char32_t cp; script sc; } cases[] {
    {.cp = U'A', .sc = script::Latin},
    {.cp = 0x03B1, .sc = script::Greek},
    {.cp = 0x0430, .sc = script::Cyrillic}, // а CYRILLIC SMALL A
    {.cp = 0x4E2D, .sc = script::Han},      // 中
  };
  for (const auto& c : cases) {
    EXPECT_EQ(script_of(c.cp), c.sc);
    EXPECT(is_scx_cp(c.sc, c.cp));
  }
}
