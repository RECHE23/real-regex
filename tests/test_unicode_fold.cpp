// Unicode simple case-fold orbits (CF1): the table + unicode_casefold, in isolation (not yet wired
// into the compiler — this slice is zero behaviour change). The orbits are validated against
// re.IGNORECASE at generation time; these contract tests are the standing second net, pinning the
// canonical (and historically bug-prone) orbits and the cross-boundary / no-contamination behaviour.
#include <algorithm>
#include <cstdint>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/compiler.hpp"

using real::detail::class_def;
using real::detail::code_range;
using real::detail::find_fold_entry;
using real::detail::unicode_casefold;

namespace {

  // The set of fold partners of \p cp (excluding itself), as a sorted check helper.
  bool partner_set_is(std::uint32_t                        cp,
                      std::initializer_list<std::uint32_t> expected)
  {
    const auto* entry {find_fold_entry(cp)};
    if (entry == nullptr) {
      return expected.size() == 0;
    }
    if (entry->count != expected.size()) {
      return false;
    }
    for (const std::uint32_t want : expected) {
      bool found {false};
      for (std::uint8_t i = 0; i < entry->count; ++i) {
        found = found || (entry->partner[i] == want);
      }
      if (!found) {
        return false;
      }
    }
    return true;
  }

  // Whether the class accepts code point \p cp (bitmap for ASCII, ranges for non-ASCII).
  bool has(const class_def&  def,
           std::uint32_t     cp)
  {
    if (cp < 0x80U) {
      return def.ascii.test(static_cast<std::uint8_t>(cp));
    }
    return std::ranges::any_of(def.ranges,
                               [cp](const code_range& r) { return cp >= r.lo && cp <= r.hi; });
  }

  class_def ascii_class(std::initializer_list<std::uint32_t> members)
  {
    class_def def;
    for (const std::uint32_t m : members) {
      def.ascii.set(static_cast<std::uint8_t>(m));
    }
    return def;
  }

  class_def range_class(std::uint32_t lo,
                        std::uint32_t hi)
  {
    class_def def;
    def.ranges.push_back({.lo = lo, .hi = hi});
    return def;
  }
} // namespace

TEST(fold_orbits_match_the_re_contract)
{
  // Canonical orbits (oracle: re.IGNORECASE). These guard against a wrong C/S/T/F status, a bad
  // merge, or a dropped cross-boundary partner.
  EXPECT(partner_set_is(0x00E9, {0x00C9}));                 // é <-> É
  EXPECT(partner_set_is(0x00DF, {0x1E9E}));                 // ß <-> ẞ ...
  EXPECT(!partner_set_is(0x00DF, {0x1E9E, 0x0073}));        // ... and ß is NOT ss (simple, not full, fold)
  EXPECT(partner_set_is(0x006B, {0x004B, 0x212A}));         // k / K / Kelvin
  EXPECT(partner_set_is(0x212A, {0x004B, 0x006B}));         // Kelvin folds back to k / K
  EXPECT(partner_set_is(0x0069, {0x0049, 0x0130, 0x0131})); // i / I / İ / ı
  EXPECT(partner_set_is(0x0073, {0x0053, 0x017F}));         // s / S / ſ (long s)
  EXPECT(partner_set_is(0x03C3, {0x03A3, 0x03C2}));         // σ / Σ / ς
  EXPECT(partner_set_is(0x0061, {0x0041}));                 // a <-> A (plain ASCII)
  EXPECT(partner_set_is(0x0030, {}));                       // '0' has no fold orbit
}

TEST(unicode_casefold_agree_ascii_no_contamination)
{
  // [a] folds to exactly {a, A}: no non-ASCII, no other ASCII pulled in (the a's orbit is minimal).
  const class_def folded {unicode_casefold(ascii_class({0x61}))};
  EXPECT(has(folded, 0x61));
  EXPECT(has(folded, 0x41));
  EXPECT(folded.ranges.empty());     // NO non-ASCII contamination
  EXPECT(!has(folded, 0x42));        // no unrelated ASCII
  EXPECT(!folded.ascii.test(0x6BU)); // no unrelated ASCII
}

TEST(unicode_casefold_bitmap_cross_boundary)
{
  // An ASCII member with a non-ASCII partner spills into the ranges: k -> {k, K} + Kelvin.
  const class_def folded {unicode_casefold(ascii_class({0x6B}))};
  EXPECT(has(folded, 0x6B));   // k
  EXPECT(has(folded, 0x4B));   // K
  EXPECT(has(folded, 0x212A)); // Kelvin (non-ASCII partner, now a range)
  EXPECT(!has(folded, 0x53));  // unrelated (S) not pulled in
}

TEST(unicode_casefold_ranges_cross_boundary_both_ways)
{
  // A non-ASCII class member gains its non-ASCII partner: [é] -> {é, É}.
  const class_def e {unicode_casefold(range_class(0x00E9, 0x00E9))};
  EXPECT(has(e, 0x00E9));  // é
  EXPECT(has(e, 0x00C9));  // É
  EXPECT(!has(e, 0x00E8)); // è not pulled in

  // The other direction: [U+0080-U+10FFFF] attracts the ASCII partners of every non-ASCII cased
  // member back into the bitmap (so this class, folded, matches k / K / s / S / i / I).
  const class_def all_non_ascii {unicode_casefold(range_class(0x80, 0x10FFFF))};
  EXPECT(all_non_ascii.ascii.test(0x6BU));  // k pulled back (from Kelvin)
  EXPECT(all_non_ascii.ascii.test(0x4BU));  // K
  EXPECT(all_non_ascii.ascii.test(0x73U));  // s (from ſ)
  EXPECT(all_non_ascii.ascii.test(0x69U));  // i (from İ / ı)
  EXPECT(!all_non_ascii.ascii.test(0x30U)); // '0' has no non-ASCII partner -> not pulled in
}

TEST(unicode_fold_unidata_version_is_pinned)
{
  // Pin the Unicode version the committed table was generated from. A CPython Unicode-data bump
  // that changes the orbits must regenerate the header (scripts/gen_unicode_fold.py) and update this.
  EXPECT(std::string_view(real::detail::unicode_fold_unidata_version) == "16.0.0");
}
