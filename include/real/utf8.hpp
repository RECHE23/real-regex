/*!
 * \file utf8.hpp
 * \brief UTF-8 position arithmetic for match iteration.
 *
 * The matching engine never needs this — multi-byte constructs are compiled
 * to byte-level alternatives. It is used only by match iteration to advance
 * past an empty match by one whole codepoint, matching Python's behaviour.
 */
#ifndef REAL_UTF8_HPP
#define REAL_UTF8_HPP

#include "version.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace real::detail {

  /*!
   * \brief Number of bytes from \p pos to the start of the next codepoint.
   *
   * Reads the lead byte at \p pos to determine the sequence length, then
   * consumes any UTF-8 continuation bytes (`10xxxxxx`) up to that length.
   * Invalid or truncated sequences advance by a single byte, so the result is
   * always in `[1, 4]` and forward progress is guaranteed.
   *
   * \param[in] text The subject text.
   * \param[in] pos  Index of the codepoint's lead byte; must be < text.size().
   * \return The codepoint's byte length, in `[1, 4]`.
   */
  constexpr std::size_t codepoint_advance(std::string_view text,
                                          std::size_t      pos)
  {
    const auto  lead   {static_cast<std::uint8_t>(text[pos])};
    std::size_t length {1};
    if (lead >= 0xF0) {
      length = 4;
    }
    else if (lead >= 0xE0) {
      length = 3;
    }
    else if (lead >= 0xC0) {
      length = 2;
    }
    std::size_t       i     {pos + 1};
    const std::size_t limit {pos + length < text.size() ? pos + length : text.size()};
    while (i < limit && (static_cast<unsigned>(static_cast<std::uint8_t>(text[i])) & 0xC0U) ==
           0x80U) {
      ++i; // skip UTF-8 continuation bytes (10xxxxxx)
    }
    return i - pos;
  }

  //! \brief The result of a strict UTF-8 decode: the code point, its byte length, and validity.
  struct decoded_codepoint
  {
    std::uint32_t cp     {}; //!< The decoded code point (meaningful only when \ref valid).
    std::size_t   length {}; //!< Bytes consumed by the sequence (1–4), or the bytes examined on failure.
    bool          valid  {}; //!< Whether the sequence is a well-formed, canonical code point.
  };

  /*!
   * \brief Strictly decodes and validates the UTF-8 sequence at `text[pos]`.
   *
   * Unlike \ref codepoint_advance (a lenient forward-progress helper for match iteration), this
   * *validates*: it rejects a lone continuation byte, a truncated sequence, an **overlong** encoding
   * (e.g. `C0 80` for NUL), a UTF-16 surrogate, and any code point above `U+10FFFF` (which also
   * covers the invalid lead bytes `0xC0`/`0xC1` and `0xF5`–`0xFF`). It is the pattern-side decoder for
   * raw UTF-8 literals; a rejection is a malformed pattern, not a silent literal.
   *
   * \param[in] text A byte sequence.
   * \param[in] pos  Index of the lead byte; must be `< text.size()`.
   * \return The decoded code point with `valid == true`, or `valid == false` on any malformation.
   */
  constexpr decoded_codepoint decode_codepoint_strict(std::string_view text,
                                                      std::size_t      pos)
  {
    const auto lead {static_cast<std::uint8_t>(text[pos])};
    if (lead < 0x80U) {
      return {.cp = lead, .length = 1, .valid = true}; // ASCII
    }
    std::size_t   length {};
    std::uint32_t cp     {};
    std::uint32_t min_cp {}; // smallest code point this length may legally encode (overlong guard)
    if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
      cp     = lead & 0x1FU;
      min_cp = 0x80U;
    }
    else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
      cp     = lead & 0x0FU;
      min_cp = 0x800U;
    }
    else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
      cp     = lead & 0x07U;
      min_cp = 0x10000U;
    }
    else {
      return {.cp = 0, .length = 1, .valid = false}; // lone continuation, or an invalid lead (0xF8–0xFF)
    }
    for (std::size_t i = 1; i < length; ++i) {
      if (pos + i >= text.size()) {
        return {.cp = 0, .length = i, .valid = false}; // truncated sequence
      }
      const auto byte {static_cast<std::uint8_t>(text[pos + i])};
      if ((byte & 0xC0U) != 0x80U) {
        return {.cp = 0, .length = i, .valid = false}; // expected a continuation byte
      }
      cp = (cp << 6U) | (byte & 0x3FU);
    }
    if (cp < min_cp) {
      return {.cp = cp, .length = length, .valid = false}; // overlong (covers 0xC0/0xC1 and E0/F0 …)
    }
    if (cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) {
      return {.cp = cp, .length = length, .valid = false}; // out of range (incl. 0xF5+) or surrogate
    }
    return {.cp = cp, .length = length, .valid = true};
  }
} // namespace real::detail

#endif // REAL_UTF8_HPP
