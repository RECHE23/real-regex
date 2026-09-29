/*!
 * \file simd.hpp
 * \brief A uniform 16-lane mask interface over two ISAs (NEON, SSE2), so the decision/loop logic that
 *        consumes it (pike.hpp) is written ONCE, with no `#if` ISA branch of its own.
 *
 * `mask_t` is opaque and per-ISA (a nibble-packed `std::uint64_t` on NEON, a bit-packed
 * `std::uint32_t` on SSE2) — the loop that drives it never touches the raw bits, only the primitives
 * below, so nothing in pike.hpp needs to know which packing is behind a given build. Two "load" entry
 * points build a mask from 16 already-loaded bytes (a small first-byte set, or a homogeneous <= 2-range
 * class); the rest — `empty`, `first_lane`, `clear_first`, `window_all_set`, `first_clear_lane`,
 * `next_set_lane` — are what a block-scan-then-verify loop needs and nothing more.
 *
 * Every function here is either intrinsics-only or a few bit ops over an opaque scalar — no eligibility
 * decision, no candidate/skip loop, no memcpy of the SUBJECT text (the caller owns that, MISRA-clean).
 * That split keeps the loop logic in pike.hpp the same C++ on every ISA, exercised by the ordinary test
 * suite whichever leg compiled. The primitives here are ISA-exclusive by construction — the NEON body
 * never compiles on x86, nor SSE2 on aarch64 — so no single runner can line-cover both; hence this
 * file's `COV_FLOOR_IGNORE` in the Makefile, the contract being guarded instead by sanitize, the fuzz
 * corpus and the twin ISA's own runs over the identical interface.
 */
#ifndef REAL_SIMD_HPP
#define REAL_SIMD_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#if defined(__ARM_NEON)
#  include <arm_neon.h>    // NEON 16-byte membership masks (aarch64 floor)
#elif defined(__SSE2__)
#  include <emmintrin.h>   // SSE2 16-byte membership masks (x86-64 floor)
#  if defined(__SSSE3__)
#    include <tmmintrin.h> // SSSE3 byte shuffle: the nibble fingerprint, where the build enables it
#  elif defined(__GNUC__) || defined(__clang__)
#    include <cpuid.h>     // whether the running CPU has SSSE3, for the fingerprint chosen at run time
#    include <tmmintrin.h> // SSSE3 byte shuffle, for functions built for it alone
#  endif
#  if defined(__AVX2__) || defined(__GNUC__) || defined(__clang__)
#    include <cpuid.h>     // whether the running CPU has AVX2, for the literal scan chosen at run time
#    include <immintrin.h> // AVX2 32-byte compares: the literal scan, built for it alone where the build lacks it
#  endif
#endif

namespace real::detail {

  //! \brief One byte in all 16 lanes, stored as bytes: built once, loaded by every block that compares against
  //!         it. Not a vector type, so it sits in `std::array` with no attribute to drop, and in a plan that
  //!         targets without vectors also carry.
  using byte_splat = std::array<std::uint8_t, 16>;

#if defined(__ARM_NEON)

  using mask_t = std::uint64_t; //!< Opaque 16-lane mask, 4 bits/lane (0xF set, 0x0 clear) — NEON has no movemask, only the narrowing shift a byte compare feeds.

  /*!
   * \brief Mask of \p buf16 against up to 8 single-byte members (the alternation first-byte set,
   *        `pattern_hints::small_set`).
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members The candidate bytes, \p count of them valid.
   * \param[in] count   Number of valid \p members (1..8).
   */
  inline mask_t load_members_mask(const std::uint8_t * buf16,
                                  const std::uint8_t * members,
                                  std::size_t          count)
  {
    const uint8x16_t  blk {vld1q_u8(buf16)};
    uint8x16_t        eq  {vdupq_n_u8(0)};
    for (std::size_t i = 0; i < count; ++i) {
      eq = vorrq_u8(eq, vceqq_u8(blk, vdupq_n_u8(members[i])));
    }
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief \ref load_members_mask against exactly eight members, unrolled: a caller with fewer repeats one of
   *        them in the unused slots (an OR with itself changes nothing). No loop is left for the compiler to
   *        unroll or not, which it decides by the size of the function around it -- in a scan loop that
   *        decision halved the speed of a six-member set.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Eight member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members8_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    const uint8x16_t blk {vld1q_u8(buf16)};
    const uint8x16_t e01 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[0])), vceqq_u8(blk, vdupq_n_u8(members[1])))};
    const uint8x16_t e23 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[2])), vceqq_u8(blk, vdupq_n_u8(members[3])))};
    const uint8x16_t e45 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[4])), vceqq_u8(blk, vdupq_n_u8(members[5])))};
    const uint8x16_t e67 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[6])), vceqq_u8(blk, vdupq_n_u8(members[7])))};
    const uint8x16_t eq  {vorrq_u8(vorrq_u8(e01, e23), vorrq_u8(e45, e67))};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief \ref load_members8_mask for at most four members: four slots, the same padding rule.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Four member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members4_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    const uint8x16_t blk {vld1q_u8(buf16)};
    const uint8x16_t e01 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[0])), vceqq_u8(blk, vdupq_n_u8(members[1])))};
    const uint8x16_t e23 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[2])), vceqq_u8(blk, vdupq_n_u8(members[3])))};
    const uint8x16_t eq  {vorrq_u8(e01, e23)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief \ref load_members8_mask for at most six members: six slots, the same padding rule.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Six member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members6_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    const uint8x16_t blk {vld1q_u8(buf16)};
    const uint8x16_t e01 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[0])), vceqq_u8(blk, vdupq_n_u8(members[1])))};
    const uint8x16_t e23 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[2])), vceqq_u8(blk, vdupq_n_u8(members[3])))};
    const uint8x16_t e45 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[4])), vceqq_u8(blk, vdupq_n_u8(members[5])))};
    const uint8x16_t eq  {vorrq_u8(vorrq_u8(e01, e23), e45)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief Mask of \p buf16 against a HOMOGENEOUS fixed-shape's shared <= 2-range set
   *        (prefilter.hpp's `class_range_count`).
   * \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] lo0   Lower bound of the first range.
   * \param[in] hi0   Upper bound of the first range.
   * \param[in] lo1   Lower bound of the second range (`lo1 > hi1` encodes "no second range").
   * \param[in] hi1   Upper bound of the second range.
   */
  inline mask_t load_range_mask(const std::uint8_t * buf16,
                                std::uint8_t         lo0,
                                std::uint8_t         hi0,
                                std::uint8_t         lo1,
                                std::uint8_t         hi1)
  {
    const uint8x16_t blk    {vld1q_u8(buf16)};
    const uint8x16_t lo0v   {vdupq_n_u8(lo0)};
    const uint8x16_t hi0v   {vdupq_n_u8(hi0)};
    const uint8x16_t lo1v   {vdupq_n_u8(lo1)};
    const uint8x16_t hi1v   {vdupq_n_u8(hi1)};
    const uint8x16_t below0 {vcltq_u8(blk, lo0v)};
    const uint8x16_t above0 {vcgtq_u8(blk, hi0v)};
    const uint8x16_t below1 {vcltq_u8(blk, lo1v)};
    const uint8x16_t above1 {vcgtq_u8(blk, hi1v)};
    const uint8x16_t out0   {vorrq_u8(below0, above0)}; // not in range0
    const uint8x16_t out1   {vorrq_u8(below1, above1)}; // not in range1 (always true when absent)
    const uint8x16_t bad    {vandq_u8(out0, out1)};     // fails both -- this lane mismatches
    const uint8x16_t good   {vmvnq_u8(bad)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(good), 4)), 0);
  }

  /*!
   * \brief Mask of the lanes where `buf16_a[l] == a` AND `buf16_b[l] == b` — the two-byte substring
   *        prefilter (prefilter.hpp's `simd_literal_scan`).
   *
   * The two windows are the SAME 16 candidate starts probed at two needle offsets: the caller loads
   * \p buf16_a at the candidate positions and \p buf16_b shifted by the offset delta, so lane `l`
   * answers "could the needle start at candidate `l`?" for both probes at once. Two bytes rejects far
   * more than one, and the survivors still get a full verify.
   *
   * Its SSE2 twin below serves the same caller: the literal scan runs on both ISAs.
   * \param[in] buf16_a 16 already-loaded bytes at the candidate starts (the caller's MISRA-clean memcpy).
   * \param[in] a       The needle byte expected at the first probe offset.
   * \param[in] buf16_b 16 already-loaded bytes at the candidate starts + delta (same, shifted).
   * \param[in] b       The needle byte expected at the second probe offset.
   */
  inline mask_t load_pair_mask(const std::uint8_t * buf16_a,
                               std::uint8_t         a,
                               const std::uint8_t * buf16_b,
                               std::uint8_t         b)
  {
    const uint8x16_t eq_a {vceqq_u8(vld1q_u8(buf16_a), vdupq_n_u8(a))};
    const uint8x16_t eq_b {vceqq_u8(vld1q_u8(buf16_b), vdupq_n_u8(b))};
    const uint8x16_t hit  {vandq_u8(eq_a, eq_b)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(hit), 4)), 0);
  }

  /*!
   * \brief Mask of the 16 starts at \p at where some pair `(lead[i] at the start, probe[i] at start + delta[i])`
   *        sits: the pairs' hits OR-ed as vectors and narrowed once, rather than one mask per pair.
   * \param[in] at    The first of the 16 starts; `at + 15 + delta[i]` must be readable for every pair.
   * \param[in] lead  Each pair's first byte, splatted.
   * \param[in] probe Each pair's second byte, splatted.
   * \param[in] delta Each pair's offset between them.
   * \param[in] count How many pairs.
   * \return The mask.
   */
  inline mask_t load_pairs_mask(const char         * at,
                                const byte_splat   * lead,
                                const byte_splat   * probe,
                                const std::uint8_t * delta,
                                std::size_t          count)
  {
    uint8x16_t blk {};
    std::memcpy(&blk, at, 16); // MISRA-clean byte loads (no pointer type-pun)
    uint8x16_t hit {vdupq_n_u8(0)};
    for (std::size_t i = 0; i < count; ++i) {
      uint8x16_t far {};
      std::memcpy(&far, at + delta[i], 16);
      hit = vorrq_u8(hit, vandq_u8(vceqq_u8(blk, vld1q_u8(lead[i].data())), vceqq_u8(far, vld1q_u8(probe[i].data()))));
    }
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(hit), 4)), 0);
  }

#if defined(__aarch64__)
  /*!
   * \brief Mask of the 16 starts at \p at whose three bytes all fall in one bucket's fingerprint: for byte `k`
   *        of a start, a bucket bit is set where both `lo[k][byte & 15]` and `hi[k][byte >> 4]` carry it, and a
   *        start is marked when some bit survives all three bytes. A table lookup per nibble (`tbl`, AArch64
   *        only), so the cost per block does not grow with the number of branches.
   * \param[in] at The first of the 16 starts; `at + 17` must be readable.
   * \param[in] lo Three 16-byte tables, low nibble to bucket bits, one per fingerprint byte.
   * \param[in] hi The same for the high nibble.
   * \return The mask.
   */
  inline mask_t load_nibble3_mask(const char                                       * at,
                                  const std::array<std::array<std::uint8_t, 16>, 3>& lo,
                                  const std::array<std::array<std::uint8_t, 16>, 3>& hi)
  {
    const uint8x16_t low4 {vdupq_n_u8(0x0F)};
    uint8x16_t       hit  {vdupq_n_u8(0xFF)};
    for (std::size_t k = 0; k < 3; ++k) {
      uint8x16_t blk         {};
      std::memcpy(&blk, at + k, 16); // MISRA-clean byte loads (no pointer type-pun)
      const uint8x16_t by_lo {vqtbl1q_u8(vld1q_u8(lo[k].data()), vandq_u8(blk, low4))};
      const uint8x16_t by_hi {vqtbl1q_u8(vld1q_u8(hi[k].data()), vshrq_n_u8(blk, 4))};
      hit                    = vandq_u8(hit, vandq_u8(by_lo, by_hi));
    }
    const uint8x16_t marked {vtstq_u8(hit, hit)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(marked), 4)), 0);
  }

#endif

  /*!
   * \brief Mask of the lanes where `buf16[l] == a` -- the single-byte scan (prefilter.hpp's
   *        `simd_byte_scan`). NEON only, like \ref load_pair_mask, and for the same reason: its caller is
   *        NEON-gated, x86-64 keeping the platform `memchr`, whose vectors are wider.
   * \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] a     The byte sought.
   */
  inline mask_t load_byte_mask(const std::uint8_t * buf16,
                               std::uint8_t         a)
  {
    const uint8x16_t eq {vceqq_u8(vld1q_u8(buf16), vdupq_n_u8(a))};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief Whether \p a occurs anywhere in the 64 bytes at \p buf64: four compares OR-ed and one horizontal
   *        max, the reject test of prefilter.hpp's `simd_byte_scan`, which builds the per-block masks
   *        (\ref load_byte_mask) only when this says there is a hit. NEON only, like its caller.
   * \param[in] buf64 64 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] a     The byte sought.
   */
  inline bool any_byte64(const std::uint8_t * buf64,
                         std::uint8_t         a)
  {
    const uint8x16_t needle {vdupq_n_u8(a)};
    const uint8x16_t lo     {vorrq_u8(vceqq_u8(vld1q_u8(buf64), needle), vceqq_u8(vld1q_u8(buf64 + 16), needle))};
    const uint8x16_t hi     {vorrq_u8(vceqq_u8(vld1q_u8(buf64 + 32), needle), vceqq_u8(vld1q_u8(buf64 + 48), needle))};
    return vmaxvq_u8(vorrq_u8(lo, hi)) != 0U;
  }

  /*! \brief `true` if no lane of \p m is set. */
  inline bool empty(mask_t m)
  {
    return m == 0U;
  }

  /*! \brief Index (0..15) of the first set lane of \p m. UB if `empty(m)`. */
  inline std::size_t first_lane(mask_t m)
  {
    return static_cast<std::size_t>(std::countr_zero(m)) >> 2U;
  }

  /*! \brief \p m with its first set lane cleared. */
  inline mask_t clear_first(mask_t m)
  {
    const std::size_t lane {first_lane(m)};
    return m & ~(static_cast<mask_t>(0xF) << (4U * lane));
  }

  /*! \brief The lanes set in \p a or \p b. */
  inline mask_t mask_or(mask_t a,
                        mask_t b)
  {
    return a | b;
  }

  /*!
   * \brief `true` if every lane in `[start, start + len)` of \p m is set. The caller clamps \p len to
   *        `16 - start`; a \p len reaching lane 16 reads as the mask's full width.
   */
  inline bool window_all_set(mask_t      m,
                             std::size_t start,
                             std::size_t len)
  {
    const mask_t want {len >= 16 ? ~mask_t {0} : ((mask_t {1} << (4U * len)) - 1U)};
    return ((m >> (4U * start)) & want) == want;
  }

  /*!
   * \brief Index (0..15, absolute — not relative to \p start) of the first CLEAR lane in
   *        `[start, start + len)` of \p m. UB if `window_all_set(m, start, len)`.
   */
  inline std::size_t first_clear_lane(mask_t      m,
                                      std::size_t start,
                                      std::size_t len)
  {
    const mask_t want  {len >= 16 ? ~mask_t {0} : ((mask_t {1} << (4U * len)) - 1U)};
    const mask_t fails {(~(m >> (4U * start))) & want};
    return start + (static_cast<std::size_t>(std::countr_zero(fails)) >> 2U);
  }

  /*! \brief Index (0..15) of the first set lane of \p m at or after \p from, or 16 if none. */
  inline std::size_t next_set_lane(mask_t      m,
                                   std::size_t from)
  {
    if (from >= 16U) {
      return 16U;
    }
    const mask_t shifted {m >> (4U * from)};
    return shifted == 0U ? 16U : from + (static_cast<std::size_t>(std::countr_zero(shifted)) >> 2U);
  }

#elif defined(__SSE2__)

  using mask_t = std::uint32_t; //!< Opaque 16-lane mask, 1 bit/lane — the form `_mm_movemask_epi8` yields.

  /*!
   * \brief Mask of \p buf16 against up to 8 single-byte members (the alternation first-byte set,
   *        `pattern_hints::small_set`). SSE2 leg of the NEON overload above.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members The candidate bytes, \p count of them valid.
   * \param[in] count   Number of valid \p members (1..8).
   */
  inline mask_t load_members_mask(const std::uint8_t * buf16,
                                  const std::uint8_t * members,
                                  std::size_t          count)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    __m128i eq  {_mm_setzero_si128()};
    for (std::size_t i = 0; i < count; ++i) {
      eq = _mm_or_si128(eq, _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))));
    }
    return static_cast<mask_t>(_mm_movemask_epi8(eq));
  }

  /*!
   * \brief \ref load_members_mask against exactly eight members, unrolled. SSE2 leg of the NEON overload above.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Eight member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members8_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    const auto    eq_at {[&](std::size_t i) { return _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))); }};
    const __m128i e01   {_mm_or_si128(eq_at(0), eq_at(1))};
    const __m128i e23   {_mm_or_si128(eq_at(2), eq_at(3))};
    const __m128i e45   {_mm_or_si128(eq_at(4), eq_at(5))};
    const __m128i e67   {_mm_or_si128(eq_at(6), eq_at(7))};
    return static_cast<mask_t>(_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(e01, e23), _mm_or_si128(e45, e67))));
  }

  /*!
   * \brief \ref load_members8_mask for at most four members. SSE2 leg of the NEON overload above.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Four member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members4_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    const auto    eq_at {[&](std::size_t i) { return _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))); }};
    return static_cast<mask_t>(_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(eq_at(0), eq_at(1)), _mm_or_si128(eq_at(2), eq_at(3)))));
  }

  /*!
   * \brief \ref load_members8_mask for at most six members. SSE2 leg of the NEON overload above.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Six member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members6_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    const auto    eq_at {[&](std::size_t i) { return _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))); }};
    const __m128i e0123 {_mm_or_si128(_mm_or_si128(eq_at(0), eq_at(1)), _mm_or_si128(eq_at(2), eq_at(3)))};
    return static_cast<mask_t>(_mm_movemask_epi8(_mm_or_si128(e0123, _mm_or_si128(eq_at(4), eq_at(5)))));
  }

  /*!
   * \brief Mask of \p buf16 against a HOMOGENEOUS fixed-shape's shared <= 2-range set
   *        (prefilter.hpp's `class_range_count`). SSE2 leg of the NEON overload above.
   * \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] lo0   Lower bound of the first range.
   * \param[in] hi0   Upper bound of the first range.
   * \param[in] lo1   Lower bound of the second range (`lo1 > hi1` encodes "no second range").
   * \param[in] hi1   Upper bound of the second range.
   */
  inline mask_t load_range_mask(const std::uint8_t * buf16,
                                std::uint8_t         lo0,
                                std::uint8_t         hi0,
                                std::uint8_t         lo1,
                                std::uint8_t         hi1)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    // SSE2 has no unsigned byte compare: bias both operands by XOR 0x80 first (an exact bijection
    // [0,255] -> [-128,127]) so signed cmplt/cmpgt on the biased values match the unsigned order.
    const __m128i bias   {_mm_set1_epi8(static_cast<char>(0x80))};
    blk = _mm_xor_si128(blk, bias);
    const __m128i lo0v   {_mm_xor_si128(_mm_set1_epi8(static_cast<char>(lo0)), bias)};
    const __m128i hi0v   {_mm_xor_si128(_mm_set1_epi8(static_cast<char>(hi0)), bias)};
    const __m128i lo1v   {_mm_xor_si128(_mm_set1_epi8(static_cast<char>(lo1)), bias)};
    const __m128i hi1v   {_mm_xor_si128(_mm_set1_epi8(static_cast<char>(hi1)), bias)};
    const __m128i below0 {_mm_cmplt_epi8(blk, lo0v)};
    const __m128i above0 {_mm_cmpgt_epi8(blk, hi0v)};
    const __m128i below1 {_mm_cmplt_epi8(blk, lo1v)};
    const __m128i above1 {_mm_cmpgt_epi8(blk, hi1v)};
    const __m128i out0   {_mm_or_si128(below0, above0)};
    const __m128i out1   {_mm_or_si128(below1, above1)};
    const __m128i bad    {_mm_and_si128(out0, out1)};
    return (~static_cast<mask_t>(_mm_movemask_epi8(bad))) & 0xFFFFU;
  }

  /*!
   * \brief The pairs' mask of 16 starts, OR-ed as vectors with one movemask. SSE2 leg of the NEON overload above,
   *        where splatting in the loop cost four instructions per byte (no byte shuffle before SSSE3).
   * \param[in] at    The first of the 16 starts; `at + 15 + delta[i]` must be readable for every pair.
   * \param[in] lead  Each pair's first byte, splatted.
   * \param[in] probe Each pair's second byte, splatted.
   * \param[in] delta Each pair's offset between them.
   * \param[in] count How many pairs.
   * \return The mask.
   */
  inline mask_t load_pairs_mask(const char         * at,
                                const byte_splat   * lead,
                                const byte_splat   * probe,
                                const std::uint8_t * delta,
                                std::size_t          count)
  {
    __m128i blk {};
    std::memcpy(&blk, at, 16); // MISRA-clean byte loads (no pointer type-pun)
    __m128i hit {_mm_setzero_si128()};
    for (std::size_t i = 0; i < count; ++i) {
      __m128i far {};
      std::memcpy(&far, at + delta[i], 16);
      __m128i a   {};
      __m128i b   {};
      std::memcpy(&a, lead[i].data(), 16);
      std::memcpy(&b, probe[i].data(), 16);
      hit = _mm_or_si128(hit, _mm_and_si128(_mm_cmpeq_epi8(blk, a), _mm_cmpeq_epi8(far, b)));
    }
    return static_cast<mask_t>(_mm_movemask_epi8(hit));
  }

#if defined(__SSSE3__) || defined(__GNUC__) || defined(__clang__)
#  if !defined(__SSSE3__)
  /*!
   * \brief Whether the running CPU has SSSE3, asked once (`cpuid` leaf 1, ECX bit 9). Not
   *        `__builtin_cpu_supports`: that reads a model libgcc or compiler-rt fills in, which not every link
   *        provides.
   * \return True when \ref load_nibble3_mask may run.
   */
  inline bool cpu_has_ssse3()
  {
    static const bool has {[] {
                             unsigned int eax {};
                             unsigned int ebx {};
                             unsigned int ecx {};
                             unsigned int edx {};
                             return __get_cpuid(1U, &eax, &ebx, &ecx, &edx) != 0 && (ecx & (1U << 9U)) != 0U;
                           }()};
    return has;
  }

#  endif

  /*!
   * \brief \ref load_nibble3_mask's x86 leg, on the SSSE3 byte shuffle, which is not in the x86-64 floor. Where
   *        the build enables SSSE3 it is an ordinary function; elsewhere it is built for SSSE3 alone and may run
   *        only once \ref cpu_has_ssse3 said so -- and it inlines only into a caller built the same way.
   * \param[in] at The first of the 16 starts; `at + 17` must be readable.
   * \param[in] lo Three 16-byte tables, low nibble to bucket bits, one per fingerprint byte.
   * \param[in] hi The same for the high nibble.
   * \return The mask.
   */
#  if !defined(__SSSE3__)
  __attribute__((target("ssse3")))
#  endif
  inline mask_t load_nibble3_mask(const char                                         * at,
                                  const std::array<std::array<std::uint8_t, 16>, 3>&   lo,
                                  const std::array<std::array<std::uint8_t, 16>, 3>&   hi)
  {
    const __m128i low4 {_mm_set1_epi8(0x0F)};
    __m128i       hit  {_mm_set1_epi8(-1)};
    for (std::size_t k = 0; k < 3; ++k) {
      __m128i blk {};
      __m128i tlo {};
      __m128i thi {};
      std::memcpy(&blk, at + k, 16); // MISRA-clean byte loads (no pointer type-pun)
      std::memcpy(&tlo, lo[k].data(), 16);
      std::memcpy(&thi, hi[k].data(), 16);
      const __m128i by_lo {_mm_shuffle_epi8(tlo, _mm_and_si128(blk, low4))};
      const __m128i by_hi {_mm_shuffle_epi8(thi, _mm_and_si128(_mm_srli_epi16(blk, 4), low4))};
      hit                 = _mm_and_si128(hit, _mm_and_si128(by_lo, by_hi));
    }
    const mask_t empty_lanes {static_cast<mask_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(hit, _mm_setzero_si128())))};
    return (~empty_lanes) & 0xFFFFU;
  }

#endif

#if defined(__AVX2__) || defined(__GNUC__) || defined(__clang__)
#  if !defined(__AVX2__)
  /*!
   * \brief Whether the running CPU can run AVX2 code, asked once: the AVX2 bit (`cpuid` leaf 7), and the
   *        operating system saving the 256-bit registers (OSXSAVE and AVX in leaf 1, then XCR0's SSE and AVX
   *        state bits) -- a CPU with AVX2 under a system that does not save them faults on the first one.
   * \return True when \ref avx2_literal_scan may run.
   */
  inline bool cpu_has_avx2()
  {
    static const bool has {[] {
                             unsigned int eax {};
                             unsigned int ebx {};
                             unsigned int ecx {};
                             unsigned int edx {};
                             if (__get_cpuid(1U, &eax, &ebx, &ecx, &edx) == 0 || (ecx & (1U << 27U)) == 0U
                                 || (ecx & (1U << 28U)) == 0U) {
                               return false;
                             }
                             unsigned int xcr0_lo {};
                             unsigned int xcr0_hi {};
                             __asm__ ("xgetbv" : "=a" (xcr0_lo), "=d" (xcr0_hi) : "c" (0U));
                             static_cast<void>(xcr0_hi);
                             if ((xcr0_lo & 6U) != 6U || __get_cpuid_max(0U, nullptr) < 7U) {
                               return false;
                             }
                             __cpuid_count(7U, 0U, eax, ebx, ecx, edx);
                             return (ebx & (1U << 5U)) != 0U;
                           }()};
    return has;
  }

#  endif

  /*!
   * \brief The 32 candidate starts at \p at where the needle's first and last bytes both sit (\ref avx2_literal_scan's
   *        block). Its own function because a lambda inside a function built for AVX2 is not built for it.
   * \param[in] at    The first candidate; `at + 31 + delta` must be readable.
   * \param[in] delta The last byte's offset from a candidate.
   * \param[in] lead  The first byte, splatted (by reference: gcc refuses a 256-bit vector passed by value in a
   *                  translation unit not built for AVX, `-Wpsabi`).
   * \param[in] trail The last byte, splatted.
   * \return One bit per candidate.
   */
#  if !defined(__AVX2__)
  __attribute__((target("avx2")))
#  endif
  inline std::uint32_t avx2_pair_block(const char*    at,
                                       std::size_t    delta,
                                       const __m256i& lead,
                                       const __m256i& trail)
  {
    __m256i a {};
    __m256i b {};
    std::memcpy(&a, at, 32); // MISRA-clean byte loads (no pointer type-pun)
    std::memcpy(&b, at + delta, 32);
    return static_cast<std::uint32_t>(
      _mm256_movemask_epi8(_mm256_and_si256(_mm256_cmpeq_epi8(a, lead), _mm256_cmpeq_epi8(b, trail))));
  }

  /*!
   * \brief \ref load_nibble3_mask on 32 starts: the same six lookups, each table repeated in both 128-bit lanes
   *        (the 256-bit byte shuffle works within a lane). Built for AVX2 where the build lacks it, like
   *        \ref avx2_literal_scan.
   * \param[in] at The first of the 32 starts; `at + 33` must be readable.
   * \param[in] lo Three 16-byte tables, low nibble to bucket bits, one per fingerprint byte.
   * \param[in] hi The same for the high nibble.
   * \return One bit per start.
   */
#  if !defined(__AVX2__)
  __attribute__((target("avx2")))
#  endif
  inline std::uint32_t avx2_nibble3_mask(const char                                         * at,
                                         const std::array<std::array<std::uint8_t, 16>, 3>&   lo,
                                         const std::array<std::array<std::uint8_t, 16>, 3>&   hi)
  {
    const __m256i low4 {_mm256_set1_epi8(0x0F)};
    __m256i       hit  {_mm256_set1_epi8(-1)};
    for (std::size_t k = 0; k < 3; ++k) {
      __m256i blk {};
      __m128i tlo {};
      __m128i thi {};
      std::memcpy(&blk, at + k, 32); // MISRA-clean byte loads (no pointer type-pun)
      std::memcpy(&tlo, lo[k].data(), 16);
      std::memcpy(&thi, hi[k].data(), 16);
      const __m256i by_lo {_mm256_shuffle_epi8(_mm256_broadcastsi128_si256(tlo), _mm256_and_si256(blk, low4))};
      const __m256i by_hi {
        _mm256_shuffle_epi8(_mm256_broadcastsi128_si256(thi), _mm256_and_si256(_mm256_srli_epi16(blk, 4), low4))};
      hit = _mm256_and_si256(hit, _mm256_and_si256(by_lo, by_hi));
    }
    return ~static_cast<std::uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(hit, _mm256_setzero_si256())));
  }

  /*!
   * \brief The two-byte literal filter (prefilter.hpp's `simd_literal_scan`, same contract) on 32-byte blocks,
   *        two a round. Where the build lacks AVX2 it is built for AVX2 alone and may run only once
   *        \ref cpu_has_avx2 said so.
   * \param[in] text    The subject.
   * \param[in] pos     Index to start searching from.
   * \param[in] literal The needle, at least two bytes.
   * \return The index of the first occurrence at or after \p pos, else `npos` (`std::size_t(-1)`).
   */
#  if !defined(__AVX2__)
  __attribute__((target("avx2")))
#  endif
  inline std::size_t avx2_literal_scan(std::string_view text,
                                       std::size_t      pos,
                                       std::string_view literal)
  {
    constexpr std::size_t not_found {static_cast<std::size_t>(-1)};
    const std::size_t     len       {literal.size()};
    if (text.size() < len) {
      return not_found;
    }
    const std::size_t last  {text.size() - len}; // last index a match could start at
    const std::size_t delta {len - 1};           // the trail byte's offset from a candidate start
    const char* const base  {text.data()};
    const __m256i     lead  {_mm256_set1_epi8(literal.front())};
    const __m256i     trail {_mm256_set1_epi8(literal[delta])};
    std::size_t       p     {pos};
    // A block covers candidates [p, p + 32): the furthest reads its trail byte at p + 31 + delta, which
    // `p + 32 <= last + 1` keeps inside the text. Masks are consumed in block then lane order, so the first
    // verified hit is the leftmost.
    while (p + 64 <= last + 1) {
      const std::uint32_t m0 {avx2_pair_block(base + p, delta, lead, trail)};
      const std::uint32_t m1 {avx2_pair_block(base + p + 32, delta, lead, trail)};
      for (std::size_t u = 0; u < 2; ++u) {
        for (std::uint32_t m {u == 0 ? m0 : m1}; m != 0U; m &= m - 1U) {
          const std::size_t cand {p + (u * 32) + static_cast<std::size_t>(std::countr_zero(m))};
          if (std::memcmp(base + cand, literal.data(), len) == 0) {
            return cand;
          }
        }
      }
      p += 64;
    }
    for (; p + 32 <= last + 1; p += 32) {
      for (std::uint32_t m {avx2_pair_block(base + p, delta, lead, trail)}; m != 0U; m &= m - 1U) {
        const std::size_t cand {p + static_cast<std::size_t>(std::countr_zero(m))};
        if (std::memcmp(base + cand, literal.data(), len) == 0) {
          return cand;
        }
      }
    }
    for (; p <= last; ++p) { // tail: fewer than 32 candidate starts left
      if (base[p] == literal.front() && base[p + delta] == literal[delta]
          && std::memcmp(base + p, literal.data(), len) == 0) {
        return p;
      }
    }
    return not_found;
  }

#endif

  /*!
   * \brief Mask of the candidate starts where both needle probes match -- the SSE2 leg of the NEON overload
   *        above (prefilter.hpp's `simd_literal_scan`).
   * \param[in] buf16_a 16 already-loaded bytes at the candidate starts (the caller's MISRA-clean memcpy).
   * \param[in] a       The needle byte expected at the first probe offset.
   * \param[in] buf16_b 16 already-loaded bytes at the candidate starts + delta (same, shifted).
   * \param[in] b       The needle byte expected at the second probe offset.
   * \return The 16-lane mask.
   */
  inline mask_t load_pair_mask(const std::uint8_t * buf16_a,
                               std::uint8_t         a,
                               const std::uint8_t * buf16_b,
                               std::uint8_t         b)
  {
    __m128i va {};
    __m128i vb {};
    std::memcpy(&va, buf16_a, 16); // MISRA-clean byte loads (no pointer type-pun)
    std::memcpy(&vb, buf16_b, 16);
    const __m128i eq_a {_mm_cmpeq_epi8(va, _mm_set1_epi8(static_cast<char>(a)))};
    const __m128i eq_b {_mm_cmpeq_epi8(vb, _mm_set1_epi8(static_cast<char>(b)))};
    return static_cast<mask_t>(_mm_movemask_epi8(_mm_and_si128(eq_a, eq_b)));
  }

  /*! \brief `true` if no lane of \p m is set. */
  inline bool empty(mask_t m)
  {
    return m == 0U;
  }

  /*! \brief Index (0..15) of the first set lane of \p m. UB if `empty(m)`. */
  inline std::size_t first_lane(mask_t m)
  {
    return static_cast<std::size_t>(std::countr_zero(m));
  }

  /*! \brief \p m with its first set lane cleared. */
  inline mask_t clear_first(mask_t m)
  {
    return m & (m - 1U);
  }

  /*! \brief The lanes set in \p a or \p b. */
  inline mask_t mask_or(mask_t a,
                        mask_t b)
  {
    return a | b;
  }

  /*!
   * \brief `true` if every lane in `[start, start + len)` of \p m is set. The caller clamps \p len to
   *        `16 - start`; a \p len reaching lane 16 reads as the mask's full width.
   */
  inline bool window_all_set(mask_t      m,
                             std::size_t start,
                             std::size_t len)
  {
    const mask_t want {len >= 16 ? 0xFFFFU : ((mask_t {1} << len) - 1U)};
    return ((m >> start) & want) == want;
  }

  /*!
   * \brief Index (0..15, absolute — not relative to \p start) of the first CLEAR lane in
   *        `[start, start + len)` of \p m. UB if `window_all_set(m, start, len)`.
   */
  inline std::size_t first_clear_lane(mask_t      m,
                                      std::size_t start,
                                      std::size_t len)
  {
    const mask_t want  {len >= 16 ? 0xFFFFU : ((mask_t {1} << len) - 1U)};
    const mask_t fails {(~(m >> start)) & want};
    return start + static_cast<std::size_t>(std::countr_zero(fails));
  }

  /*! \brief Index (0..15) of the first set lane of \p m at or after \p from, or 16 if none. */
  inline std::size_t next_set_lane(mask_t      m,
                                   std::size_t from)
  {
    if (from >= 16U) {
      return 16U;
    }
    const mask_t shifted {m >> from};
    return shifted == 0U ? 16U : from + static_cast<std::size_t>(std::countr_zero(shifted));
  }

#endif
} // namespace real::detail

#endif // REAL_SIMD_HPP
