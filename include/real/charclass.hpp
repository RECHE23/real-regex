/*!
 * \file charclass.hpp
 * \brief 256-bit byte set with O(1) membership, fully constexpr.
 *
 * The engine only ever tests bitmaps: negation and "one whole codepoint"
 * semantics are resolved at compile time (see compiler.hpp), never at match
 * time. Also provides the ASCII sets behind `\d`, `\w` and `\s`.
 */
#ifndef REAL_CHARCLASS_HPP
#define REAL_CHARCLASS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace real::detail {

  /*!
   * \brief A set of byte values (0–255) as a 256-bit bitmap.
   *
   * Membership, insertion and complement are all O(1) or O(256) and constexpr.
   * All bit manipulation uses unsigned operands (MISRA forbids signed bitwise).
   */
  struct char_class
  {
    std::array<std::uint64_t, 4> bits {}; //!< Bitmap; bit `b` is byte `b's` membership.

    /*!
     * \brief Adds byte \p b to the set.
     * \param[in] b The byte to insert.
     */
    constexpr void set(std::uint8_t b)
    {
      const unsigned bit {b};
      bits[bit >> 6U] |= std::uint64_t {1} << (bit & 63U);
    }

    /*!
     * \brief Adds the inclusive byte range `[lo, hi]` to the set.
     * \param[in] lo First byte of the range.
     * \param[in] hi Last byte of the range (inclusive).
     */
    constexpr void set_range(std::uint8_t lo,
                             std::uint8_t hi)
    {
      for (unsigned b = lo; b <= hi; ++b) {
        set(static_cast<std::uint8_t>(b));
      }
    }

    /*!
     * \brief Unions \p other into this set.
     * \param[in] other The set whose members are added to this one.
     */
    constexpr void merge(const char_class& other)
    {
      for (std::size_t i = 0; i < bits.size(); ++i) {
        bits[i] |= other.bits[i];
      }
    }

    /*!
     * \brief Complements the ASCII half (bytes 0–127) only.
     *
     * Bytes >= 0x80 are left untouched; the compiler handles non-ASCII
     * codepoints as explicit UTF-8 multi-byte alternatives instead.
     */
    constexpr void invert_ascii()
    {
      bits[0] = ~bits[0];
      bits[1] = ~bits[1];
    }

    /*!
     * \brief Full 256-bit complement (binary mode: raw bytes, no UTF-8).
     */
    constexpr void invert()
    {
      for (auto& word : bits) {
        word = ~word;
      }
    }

    /*!
     * \brief Tests membership of byte \p b.
     * \param[in] b The byte to test.
     * \return `true` if \p b is in the set.
     */
    [[nodiscard]] constexpr bool test(std::uint8_t b) const
    {
      const unsigned bit {b};
      return ((bits[bit >> 6U] >> (bit & 63U)) & 1U) != 0;
    }

    /*!
     * \brief Reports whether the set has no members.
     * \return `true` if the set is empty.
     */
    [[nodiscard]] constexpr bool empty() const
    {
      return (bits[0] | bits[1] | bits[2] | bits[3]) == 0;
    }

    constexpr bool operator==(const char_class&) const = default;
  };

  /*!
   * \brief Closes \p cc under ASCII case folding.
   *
   * Whenever a letter is present its other-case twin is added. Applied to a
   * class \e before negation, so `[^a]` with `icase` rejects both
   * 'a' and 'A', matching Python.
   *
   * \param[in,out] cc The class to fold in place.
   */
  constexpr void fold_ascii_case(char_class& cc)
  {
    for (std::uint8_t c = 'A'; c <= 'Z'; ++c) {
      const auto lower {static_cast<std::uint8_t>(c + 32)};
      if (cc.test(c)) {
        cc.set(lower);
      }
      if (cc.test(lower)) {
        cc.set(c);
      }
    }
  }

  /*!
   * \brief Reports whether \p b is an ASCII "word" byte (`[0-9A-Za-z_]`).
   * \param[in] b The byte to classify.
   * \return `true` for ASCII word bytes, used by `\b` / `\w`.
   */
  [[nodiscard]] constexpr bool is_ascii_word_byte(std::uint8_t b)
  {
    return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') ||
           b == '_';
  }

  /*!
   * \brief The ASCII digit set behind `\d` (Python `re.ASCII` semantics).
   * \return The set `[0-9]`.
   */
  constexpr char_class digit_set()
  {
    char_class cc;
    cc.set_range('0', '9');
    return cc;
  }

  /*!
   * \brief The ASCII word set behind `\w`.
   * \return The set `[0-9A-Za-z_]`.
   */
  constexpr char_class word_set()
  {
    char_class cc;
    cc.set_range('0', '9');
    cc.set_range('A', 'Z');
    cc.set_range('a', 'z');
    cc.set('_');
    return cc;
  }

  /*!
   * \brief The ASCII whitespace set behind `\s`.
   * \return The set `[ \t\n\r\f\v]`.
   */
  constexpr char_class space_set()
  {
    char_class cc;
    cc.set(' ');
    cc.set('\t');
    cc.set('\n');
    cc.set('\r');
    cc.set('\f');
    cc.set('\v');
    return cc;
  }

  // --- UTF-8 byte-class sets -------------------------------------------------
  // The single source of truth for how `.` and negated classes expand to bytes:
  // the compiler emits these sets (compiler.hpp) and the prefilter recognizes
  // the same shape (prefilter.hpp). Keeping them here keeps the two in lock-step.

  /*!
   * \brief The UTF-8 continuation-byte set `10xxxxxx`.
   * \return The set `[0x80, 0xBF]`.
   */
  constexpr char_class utf8_cont_set()
  {
    char_class cc;
    cc.set_range(0x80, 0xBF);
    return cc;
  }

  /*!
   * \brief The lead-byte set of a 2-byte UTF-8 sequence.
   * \return The set `[0xC2, 0xDF]`.
   */
  constexpr char_class utf8_lead2_set()
  {
    char_class cc;
    cc.set_range(0xC2, 0xDF);
    return cc;
  }

  /*!
   * \brief The lead-byte set of a 3-byte UTF-8 sequence.
   * \return The set `[0xE0, 0xEF]`.
   */
  constexpr char_class utf8_lead3_set()
  {
    char_class cc;
    cc.set_range(0xE0, 0xEF);
    return cc;
  }

  /*!
   * \brief The lead-byte set of a 4-byte UTF-8 sequence.
   * \return The set `[0xF0, 0xF4]`.
   */
  constexpr char_class utf8_lead4_set()
  {
    char_class cc;
    cc.set_range(0xF0, 0xF4);
    return cc;
  }
} // namespace real::detail

#endif // REAL_CHARCLASS_HPP
