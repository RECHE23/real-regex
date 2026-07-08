/*!
 * \file simd.hpp
 * \brief Pure SIMD primitives: 16-byte membership masks, one per shape (small first-byte set, a
 *        homogeneous <= 2-range class), NEON and SSE2.
 *
 * Every function here is intrinsics-only: load 16 bytes, compare, pack a mask, return it — no
 * eligibility decision, no loop, no candidate/skip logic, no memcpy of the SUBJECT text (the caller
 * owns that, MISRA-clean). That split is deliberate: the decision/loop logic in pike.hpp is the same
 * C++ on every ISA and is exercised by the ordinary test suite regardless of which SIMD leg compiled;
 * the intrinsics here are, by construction, ISA-exclusive (the NEON body never compiles on x86 and vice
 * versa), so a single-ISA CI runner can never line-cover both — see the Makefile's `COV_FLOOR_IGNORE`
 * for this file (guarded instead by sanitize, the fuzz corpus, the correctness nets in test_quantifiers,
 * and — the practical proof — the twin ISA's own coverage of the identical contract).
 */
#ifndef REAL_SIMD_HPP
#define REAL_SIMD_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/std/regex.hpp>).

#include "real/version.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__ARM_NEON)
#  include <arm_neon.h>  // NEON 16-byte membership masks (aarch64 floor)
#elif defined(__SSE2__)
#  include <emmintrin.h> // SSE2 16-byte membership masks (x86-64 floor)
#endif

namespace real::detail {

#if defined(__ARM_NEON)

  /*!
   * \brief Nibble-packed (4 bits/lane, 0xF = member) mask of \p buf16 against up to 8 single-byte
   *        members (the alternation first-byte set, `pattern_hints::small_set`).
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members The candidate bytes, \p count of them valid.
   * \param[in] count   Number of valid \p members (1..8).
   */
  inline std::uint64_t simd_small_set_nibmask16(const std::uint8_t * buf16,
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
   * \brief Nibble-packed (4 bits/lane, 0xF = in-range) mask of \p buf16 against a HOMOGENEOUS
   *        fixed-shape's shared <= 2-range set (prefilter.hpp's `class_range_count`).
   * \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] lo0    Lower bound of the first range.
   * \param[in] hi0    Upper bound of the first range.
   * \param[in] lo1    Lower bound of the second range (`lo1 > hi1` encodes "no second range").
   * \param[in] hi1    Upper bound of the second range.
   */
  inline std::uint64_t simd_range_good_nibmask16(const std::uint8_t * buf16,
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

#elif defined(__SSE2__)

  /*!
   * \brief 1-bit/lane mask of \p buf16 against up to 8 single-byte members (the alternation
   *        first-byte set, `pattern_hints::small_set`). SSE2 leg of \ref simd_small_set_nibmask16.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members The candidate bytes, \p count of them valid.
   * \param[in] count   Number of valid \p members (1..8).
   */
  inline std::uint32_t simd_small_set_mask16(const std::uint8_t * buf16,
                                             const std::uint8_t * members,
                                             std::size_t          count)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    __m128i eq  {_mm_setzero_si128()};
    for (std::size_t i = 0; i < count; ++i) {
      eq = _mm_or_si128(eq, _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))));
    }
    return static_cast<std::uint32_t>(_mm_movemask_epi8(eq));
  }

  /*!
   * \brief 1-bit/lane mask of \p buf16 against a HOMOGENEOUS fixed-shape's shared <= 2-range set
   *        (prefilter.hpp's `class_range_count`). SSE2 leg of \ref simd_range_good_nibmask16.
   * \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] lo0    Lower bound of the first range.
   * \param[in] hi0    Upper bound of the first range.
   * \param[in] lo1    Lower bound of the second range (`lo1 > hi1` encodes "no second range").
   * \param[in] hi1    Upper bound of the second range.
   */
  inline std::uint32_t simd_range_good_mask16(const std::uint8_t * buf16,
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
    return (~static_cast<std::uint32_t>(_mm_movemask_epi8(bad))) & 0xFFFFU;
  }

#endif
} // namespace real::detail

#endif // REAL_SIMD_HPP
