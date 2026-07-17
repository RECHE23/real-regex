//! Hardening #4 — C ABI contract pins (enums + flags cross-check vs real::flags).
//!
//! The golden signature surface is enforced by tools/gen_capi_abi_golden.py
//! (`python3 tools/gen_capi_abi_golden.py --check` / `make check-capi-abi`).
//! This translation unit pins the *values* bindings pass through raw: REAL_ERR_*,
//! REAL_MODE_*, and the documented flag bitmask, which must equal real::flags
//! for every bit the C header documents (a desync = silent wrong mode in
//! Python/Rust/Go bindings).
#include <sciforge/test/framework.hpp>

#include <real/core/program.hpp>
#include <real_capi.h>

#include <cstdint>

// --- enum ordinals (compile-time + runtime) ---------------------------------

static_assert(REAL_ERR_NONE == 0);
static_assert(REAL_ERR_SYNTAX == 1);
static_assert(REAL_ERR_UNSUPPORTED == 2);
static_assert(REAL_MODE_SEARCH == 0);
static_assert(REAL_MODE_MATCH == 1);
static_assert(REAL_MODE_FULLMATCH == 2);

TEST(capi_abi_error_ordinals_pinned)
{
  EXPECT_EQ(REAL_ERR_NONE, 0);
  EXPECT_EQ(REAL_ERR_SYNTAX, 1);
  EXPECT_EQ(REAL_ERR_UNSUPPORTED, 2);
}

TEST(capi_abi_mode_ordinals_pinned)
{
  EXPECT_EQ(REAL_MODE_SEARCH, 0);
  EXPECT_EQ(REAL_MODE_MATCH, 1);
  EXPECT_EQ(REAL_MODE_FULLMATCH, 2);
}

// --- flags: C documented bitmask == real::flags (bindings pass flags raw) ---

static_assert(static_cast<std::uint32_t>(real::flags::none) == 0U);
static_assert(static_cast<std::uint32_t>(real::flags::icase) == 1U);
static_assert(static_cast<std::uint32_t>(real::flags::multiline) == 2U);
static_assert(static_cast<std::uint32_t>(real::flags::dotall) == 4U);
static_assert(static_cast<std::uint32_t>(real::flags::bytes) == 8U);
static_assert(static_cast<std::uint32_t>(real::flags::verbose) == 16U);
static_assert(static_cast<std::uint32_t>(real::flags::ecma) == 32U);
static_assert(static_cast<std::uint32_t>(real::flags::ascii) == 64U);
static_assert(static_cast<std::uint32_t>(real::flags::dollar_endonly) == 128U);
// allow_raw_byte is C++-only (compat::re2); not in the C ABI flag table — not pinned here.

TEST(capi_abi_flags_match_real_flags)
{
  // Same numbers the C header documents (icase=1 … dollar_endonly=128).
  EXPECT_EQ(static_cast<std::uint32_t>(real::flags::icase), 1U);
  EXPECT_EQ(static_cast<std::uint32_t>(real::flags::multiline), 2U);
  EXPECT_EQ(static_cast<std::uint32_t>(real::flags::dotall), 4U);
  EXPECT_EQ(static_cast<std::uint32_t>(real::flags::bytes), 8U);
  EXPECT_EQ(static_cast<std::uint32_t>(real::flags::verbose), 16U);
  EXPECT_EQ(static_cast<std::uint32_t>(real::flags::ecma), 32U);
  EXPECT_EQ(static_cast<std::uint32_t>(real::flags::ascii), 64U);
  EXPECT_EQ(static_cast<std::uint32_t>(real::flags::dollar_endonly), 128U);
}
