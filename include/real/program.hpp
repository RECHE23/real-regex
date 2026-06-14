/*!
 * \file program.hpp
 * \brief Compiled form of a pattern and the public flags / error types.
 *
 * Defines the NFA instruction set executed by the engine, the heap-allocated
 * program the compiler produces, the non-owning view the engine runs over,
 * the compilation \ref real::flags, and \ref real::regex_error (thrown on an
 * invalid pattern).
 */
#ifndef REAL_PROGRAM_HPP
#define REAL_PROGRAM_HPP

#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "charclass.hpp"

namespace real {

  /*!
   * \brief Sentinel for "no position" / unset capture slot (akin to std::string::npos).
   */
  inline constexpr std::size_t npos {std::numeric_limits<std::size_t>::max()};

  /*!
   * \brief Compilation flags, mirroring Python's `re.I`, `re.M` and `re.S`.
   *
   * Combinable with \ref operator|. Case folding is ASCII-only, consistent with
   * the library's character-class semantics.
   */
  enum class flags : std::uint8_t
  {
    none      = 0,  //!< No flags.
    icase     = 1,  //!< Case-insensitive (ASCII).
    multiline = 2,  //!< `^` and `$` also match at line boundaries.
    dotall    = 4,  //!< `.` also matches `\n`.
    bytes     = 8,  //!< Binary mode: `.` and `[^…]` match raw bytes, not codepoints.
    verbose   = 16, //!< Verbose mode (`re.X`): ignore unescaped whitespace and `#` comments outside classes.
  };

  /*!
   * \brief Bitwise-OR of two flag sets.
   * \param[in] a First flag set.
   * \param[in] b Second flag set.
   * \return The union of \p a and \p b.
   */
  constexpr flags operator|(flags a,
                            flags b)
  {
    return static_cast<flags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
  }

  /*!
   * \brief Bitwise-AND of two flag sets.
   * \param[in] a First flag set.
   * \param[in] b Second flag set.
   * \return The intersection of \p a and \p b.
   */
  constexpr flags operator&(flags a,
                            flags b)
  {
    return static_cast<flags>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
  }

  /*!
   * \brief Tests whether \p flag is set in \p value.
   * \param[in] value The flag set to query.
   * \param[in] flag  The single flag to look for.
   * \return `true` if \p flag is present in \p value.
   */
  constexpr bool has_flag(flags value,
                          flags flag)
  {
    return (value & flag) != flags::none;
  }

  /*!
   * \brief Exception thrown for an invalid pattern (or one exceeding a limit).
   *
   * In a constexpr context (`static_regex`), reaching the throw is a
   * compile-time error, with the message appearing in the diagnostic trace.
   */
  class regex_error : public std::exception
  {
  public:

    /*!
     * \brief Builds the error.
     * \param[in] message  Human-readable cause.
     * \param[in] position Byte offset in the pattern where the error was found.
     */
    regex_error(const std::string& message,
                std::size_t        position)
      : message_("regex_error at " + std::to_string(position) + ": " + message),
        position_(position)
    {}

    /*!
     * \return The formatted error message (with position).
     */
    [[nodiscard]] const char* what() const noexcept override
    {
      return message_.c_str();
    }

    /*!
     * \return The byte offset in the pattern where the error was found.
     */
    [[nodiscard]] std::size_t position() const noexcept
    {
      return position_;
    }

  private:

    std::string message_;  //!< Formatted message returned by what().
    std::size_t position_; //!< Offset in the pattern text.
  };

  namespace detail {

    /*!
     * \brief NFA instruction opcodes executed by the Pike VM.
     */
    enum class opcode : std::uint8_t
    {
      byte,            //!< Consume one byte equal to arg8; fall through to pc+1.
      klass,           //!< Consume one byte in classes[arg16]; fall through to pc+1.
      split,           //!< Epsilon-branch to x (preferred) and y.
      jump,            //!< Epsilon-jump to x.
      save,            //!< Store current position in slot arg16; fall through (epsilon).
      assert_position, //!< Epsilon; proceeds only if assertion arg8 holds here.
      match,           //!< Accept.
    };

    /*!
     * \brief Kind of zero-width assertion carried in `assert_position`'s arg8.
     *
     * Multiline and trailing-newline subtleties are resolved at compile time; the
     * engine only evaluates these predicates at a position.
     */
    enum class assert_kind : std::uint8_t
    {
      text_start,                //!< `\A`, and `^` without multiline.
      text_end,                  //!< `\Z`.
      text_end_or_final_newline, //!< `$` without multiline (Python semantics).
      line_start,                //!< `^` with multiline.
      line_end,                  //!< `$` with multiline.
      word_boundary,             //!< `\b` (ASCII word characters).
      not_word_boundary,         //!< `\B`.
      word_start,                //!< `\<` (non-word/start on the left, word on the right).
      word_end,                  //!< `\>` (word on the left, non-word/end on the right).
    };

    /*!
     * \brief One NFA instruction. Field meaning depends on \ref op.
     */
    struct instr
    {
      opcode        op;       //!< The operation.
      std::uint8_t  arg8  {}; //!< Byte literal, or \ref assert_kind, depending on op.
      std::uint16_t arg16 {}; //!< Class index (klass) or capture slot (save).
      std::int32_t  x     {}; //!< Primary branch target (split/jump).
      std::int32_t  y     {}; //!< Secondary branch target (split).
    };

    /*!
     * \brief Search-acceleration hints extracted from a compiled program.
     *
     * Filled by `analyze_program` (prefilter.hpp). The engine consults them to
     * skip positions that cannot start a match and to take fast paths; they never
     * change \e what matches, only how fast.
     */
    struct pattern_hints
    {
      std::array<char, 16> prefix                {};   //!< Required literal prefix (possibly truncated).
      std::uint8_t         prefix_size           {};   //!< Valid bytes in \ref prefix.
      bool                 anchored_start        {};   //!< `\A` / `^` (no multiline): only position 0.
      bool                 line_anchored         {};   //!< `^` multiline: position 0 or after `\n`.
      bool                 first_bytes_valid     {};   //!< False when an empty match is possible.
      std::int16_t         single_first          {-1}; //!< The unique possible first byte, or -1.
      char_class           first_bytes;                //!< All possible first bytes.
      std::int32_t         greedy_class_loop     {-1}; //!< Class index if the whole pattern is "class+", else -1.
      bool                 fixed_shape           {};   //!< Whole pattern is a fixed-width byte/klass sequence (no branches/asserts/captures).
      std::int32_t         codepoint_class_ascii {-1}; //!< ASCII-class index when the whole pattern is `.`/negated-class (optionally `+`), else -1.
      bool                 codepoint_class_plus  {};   //!< The \ref codepoint_class_ascii pattern is a greedy `+` loop (vs a single codepoint).
      bool                 fixed_alternation     {};   //!< Whole pattern is an alternation of straight-line branches (no captures/asserts).

      /*!
       * \brief Length of the pure-literal match, or 0.
       *
       * Non-zero when the whole pattern is a fixed literal (the prefix bytes are
       * the entire match content, possibly with internal group saves but no
       * branches or further consuming ops). Enables a direct slot-replay bypass
       * of the full Pike VM — the major win for "search for a fixed string".
       */
      std::uint8_t exact_literal_len {};
    };

    /*!
     * \brief A named capture group.
     *
     * The name is stored as a byte range into the pattern text rather than an
     * owned string, keeping the type constexpr-friendly.
     */
    struct named_group
    {
      std::int32_t group {}; //!< Capture group number.
      std::int32_t begin {}; //!< Start offset of the name in the pattern text.
      std::int32_t end   {}; //!< End offset (exclusive) of the name.
    };

    /*!
     * \brief Non-owning view of a compiled program — what the engine executes.
     *
     * The spans point into storage that must outlive the view (the owning regex
     * object). Both the dynamic and static storage policies expose one of these.
     */
    struct program_view
    {
      std::span<const instr>       code;           //!< The instruction stream.
      std::span<const char_class>  classes;        //!< Interned character classes.
      std::span<const named_group> names;          //!< Named capture groups.
      std::uint16_t                slot_count {2}; //!< `2 * (capture groups + 1)`.
      bool                         byte_mode  {};  //!< \ref flags::bytes mode — positions are raw bytes.
      pattern_hints                hints;          //!< Search-acceleration hints.
    };

    /*!
     * \brief Owning, heap-allocated program: the storage backing `real::regex`.
     */
    struct dynamic_program
    {
      std::vector<instr>       code;           //!< The instruction stream.
      std::vector<char_class>  classes;        //!< Interned character classes.
      std::vector<named_group> names;          //!< Named capture groups.
      std::uint16_t            slot_count {2}; //!< `2 * (capture groups + 1)`.
      bool                     byte_mode  {};  //!< \ref flags::bytes mode.
      pattern_hints            hints;          //!< Search-acceleration hints.

      /*!
       * \return A non-owning \ref program_view over this program.
       */
      [[nodiscard]] constexpr program_view view() const
      {
        return {.code       = std::span<const instr>(code),
                .classes    = std::span<const char_class>(classes),
                .names      = std::span<const named_group>(names),
                .slot_count = slot_count,
                .byte_mode  = byte_mode,
                .hints      = hints};
      }
    };
  } // namespace detail
} // namespace real

#endif // REAL_PROGRAM_HPP
