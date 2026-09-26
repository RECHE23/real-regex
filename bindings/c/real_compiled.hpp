/*!
 * \file real_compiled.hpp
 * \brief A C++ regex over the compiled REAL library: what a translation unit includes when it links
 *        `real::capi` instead of compiling the engine itself.
 *
 * `<real/real.hpp>` is the whole engine: a translation unit that includes it parses the engine and its
 * Unicode tables and instantiates and optimises the matcher, seconds of compilation per unit. This header
 * includes only `real_capi.h` and the standard library: the engine is compiled once, into the library, and
 * every call crosses the C ABI. The price is that boundary: no `static_regex`, no inlining into the
 * caller, and one call per match.
 *
 * The types own what they return. A \ref real::compiled::match_result holds its spans by value and a view of the
 * subject, never a view into the regex, so it outlives the regex that produced it; the subject must outlive
 * the match, as with `std::string_view`.
 *
 * \code
 * #include <real_compiled.hpp>   // link real::capi
 *
 * const real::compiled::regex re {R"((\w+)@(\w+))"};
 * if (const auto m {re.search("mail bob@host now")}) {
 *   std::string_view user {m.str(1)};   // "bob"
 * }
 * \endcode
 */
#ifndef REAL_COMPILED_HPP
#define REAL_COMPILED_HPP

#include "real_capi.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace real::compiled {

  //! \brief Offset of an unset group (the C ABI's `SIZE_MAX`).
  inline constexpr std::size_t npos {std::numeric_limits<std::size_t>::max()};

  /*!
   * \brief Compile flags, the numbering of `real::flags` and of the C ABI (not Python's; see real_capi.h).
   */
  enum class flags : std::uint32_t
  {
    none           = 0,   //!< No flag.
    icase          = 1,   //!< Case-insensitive.
    multiline      = 2,   //!< `^`/`$` at line boundaries.
    dotall         = 4,   //!< `.` matches a newline.
    bytes          = 8,   //!< Byte semantics: no UTF-8 decoding.
    verbose        = 16,  //!< Whitespace and `#` comments in the pattern are ignored.
    ecma           = 32,  //!< ECMAScript `$` and `.`.
    ascii          = 64,  //!< `\w`, `\d`, `\s`, `\b` are ASCII-only.
    dollar_endonly = 128, //!< `$` matches only at the very end.
  };

  //! \brief Union of two flag sets.
  //! \param[in] a First set.
  //! \param[in] b Second set.
  //! \return Both.
  [[nodiscard]] constexpr flags operator|(flags a,
                                          flags b) noexcept
  {
    return static_cast<flags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
  }

  /*!
   * \brief A pattern the library refused, or an operation it failed.
   */
  class error : public std::runtime_error
  {
  public:
    /*!
     * \brief An error with the library's cause.
     * \param[in] cause    The message.
     * \param[in] code     A `REAL_ERR_*` value from real_capi.h (0 when the failure was not a compile).
     * \param[in] position The byte offset in the pattern, or \ref npos.
     */
    error(const std::string& cause,
          int                code,
          std::size_t        position)
      : std::runtime_error(cause),
        code_(code),
        position_(position)
    {}

    //! \brief The `REAL_ERR_*` classification of a compile failure; 0 otherwise.
    //! \return The code.
    [[nodiscard]] int code() const noexcept
    {
      return code_;
    }

    //! \brief Byte offset in the pattern the failure was reported at, or \ref npos.
    //! \return The offset.
    [[nodiscard]] std::size_t position() const noexcept
    {
      return position_;
    }

  private:
    int         code_;
    std::size_t position_;
  };

  /*!
   * \brief One match: its spans, owned, and a view of the subject they index.
   */
  class match_result
  {
  public:
    //! \brief No match.
    match_result() = default;

    /*!
     * \brief A match over \p subject with \p spans (start and end per slot, \ref npos when unset).
     * \param[in] subject The text the spans index.
     * \param[in] spans   Two offsets per slot, group 0 first.
     */
    match_result(std::string_view         subject,
                 std::vector<std::size_t> spans)
      : subject_(subject),
        spans_(std::move(spans))
    {}

    //! \brief Whether this is a match.
    //! \return True when it is.
    [[nodiscard]] explicit operator bool() const noexcept
    {
      return !spans_.empty();
    }

    //! \brief Slots: the capturing groups plus group 0; 0 for no match.
    //! \return The count.
    [[nodiscard]] std::size_t size() const noexcept
    {
      return spans_.size() / 2U;
    }

    //! \brief Whether group \p g took part in the match.
    //! \param[in] g The group.
    //! \return True when it did.
    [[nodiscard]] bool matched(std::size_t g) const noexcept
    {
      return g < size() && spans_[2U * g] != npos;
    }

    //! \brief Where group \p g starts, or \ref npos.
    //! \param[in] g The group (0 is the whole match).
    //! \return The offset.
    [[nodiscard]] std::size_t start(std::size_t g = 0) const noexcept
    {
      return g < size() ? spans_[2U * g] : npos;
    }

    //! \brief Where group \p g ends, or \ref npos.
    //! \param[in] g The group (0 is the whole match).
    //! \return The offset.
    [[nodiscard]] std::size_t end(std::size_t g = 0) const noexcept
    {
      return g < size() ? spans_[(2U * g) + 1U] : npos;
    }

    //! \brief The text of group \p g; empty when it did not take part.
    //! \param[in] g The group (0 is the whole match).
    //! \return A view of the subject.
    [[nodiscard]] std::string_view str(std::size_t g = 0) const noexcept
    {
      return matched(g) ? subject_.substr(start(g), end(g) - start(g)) : std::string_view {};
    }

  private:
    std::string_view         subject_;
    std::vector<std::size_t> spans_;
  };

  /*!
   * \brief A compiled pattern: a handle to the library's immutable program, cheap to copy and safe to share
   *        between threads (see real_capi.h for the thread-safety contract).
   */
  class regex
  {
  public:
    /*!
     * \brief Compiles \p pattern.
     * \param[in] pattern The pattern.
     * \param[in] f       Flags.
     * \throws real::compiled::error When the pattern is refused; `code()` and `position()` say why and where.
     */
    explicit regex(std::string_view pattern,
                   flags            f = flags::none)
      : pattern_(pattern)
    {
      std::string buffer(256, '\0');
      int         code {0};
      std::size_t position {npos};
      real_regex* compiled {real_compile_ex(pattern.data(), pattern.size(), static_cast<std::uint32_t>(f),
                                            buffer.data(), buffer.size(), &code, &position)};
      if (compiled == nullptr) {
        buffer.resize(buffer.find('\0'));
        throw error(buffer, code, position);
      }
      handle_.reset(compiled, &real_free);
      slots_ = real_group_count(compiled);
    }

    //! \brief The pattern as given.
    //! \return The pattern.
    [[nodiscard]] const std::string& pattern() const noexcept
    {
      return pattern_;
    }

    //! \brief Capturing groups, group 0 excluded.
    //! \return The count.
    [[nodiscard]] std::size_t group_count() const noexcept
    {
      return slots_ - 1U;
    }

    /*!
     * \brief The name of group \p g, or empty for an unnamed group.
     * \param[in] g The group.
     * \return The name.
     */
    [[nodiscard]] std::string group_name(std::size_t g) const
    {
      const std::size_t length {real_group_name(handle_.get(), g, nullptr, 0)};
      std::string       name(length + 1U, '\0');
      static_cast<void>(real_group_name(handle_.get(), g, name.data(), name.size()));
      name.resize(length);
      return name;
    }

    /*!
     * \brief The group named \p name, or \ref npos.
     * \param[in] name The name.
     * \return The group number.
     */
    [[nodiscard]] std::size_t group_index(std::string_view name) const
    {
      for (std::size_t g {1}; g < slots_; ++g) {
        if (group_name(g) == name) {
          return g;
        }
      }
      return npos;
    }

    //! \brief The leftmost match in [\p start, \p end) of \p text (Python `re.search`).
    //! \param[in] text  The subject.
    //! \param[in] start Where the search starts (an anchor position, not a slice).
    //! \param[in] end   Where the subject ends for this search.
    //! \return The match, or no match.
    [[nodiscard]] match_result search(std::string_view text,
                               std::size_t      start = 0,
                               std::size_t      end   = npos) const
    {
      return run(text, start, end, REAL_MODE_SEARCH);
    }

    //! \brief A match anchored at \p start (Python `re.match`).
    //! \param[in] text  The subject.
    //! \param[in] start The anchor.
    //! \param[in] end   Where the subject ends for this attempt.
    //! \return The match, or no match.
    [[nodiscard]] match_result match(std::string_view text,
                                 std::size_t      start = 0,
                                 std::size_t      end   = npos) const
    {
      return run(text, start, end, REAL_MODE_MATCH);
    }

    //! \brief A match of the whole region [\p start, \p end) (Python `re.fullmatch`).
    //! \param[in] text  The subject.
    //! \param[in] start The region's start.
    //! \param[in] end   The region's end.
    //! \return The match, or no match.
    [[nodiscard]] match_result fullmatch(std::string_view text,
                                  std::size_t      start = 0,
                                  std::size_t      end   = npos) const
    {
      return run(text, start, end, REAL_MODE_FULLMATCH);
    }

    /*!
     * \brief Every non-overlapping match of \p text, in order (Python `re.finditer`).
     * \param[in] text The subject.
     * \return The matches.
     */
    [[nodiscard]] std::vector<match_result> find_all(std::string_view text) const
    {
      const std::unique_ptr<real_iter, void (*)(real_iter*)> it {real_find_iter(handle_.get(), text.data(), text.size()),
                                                                 &real_iter_free};
      if (!it) {
        throw error("real::compiled: the library could not start an iteration", 0, npos);
      }
      std::vector<match_result> out;
      while (true) {
        std::vector<std::size_t> spans(2U * slots_);
        const int                step {real_iter_next(it.get(), spans.data())};
        if (step == 0) {
          return out;
        }
        if (step < 0) {
          throw error("real::compiled: the library failed during an iteration", 0, npos);
        }
        out.emplace_back(text, std::move(spans));
      }
    }

    /*!
     * \brief How many non-overlapping matches \p text holds, without building them.
     * \param[in] text The subject.
     * \return The count.
     */
    [[nodiscard]] std::size_t count(std::string_view text) const
    {
      const std::size_t n {real_count_matches(handle_.get(), text.data(), text.size())};
      if (n == npos) {
        throw error("real::compiled: the library failed while counting", 0, npos);
      }
      return n;
    }

    /*!
     * \brief \p text with up to \p count matches replaced by \p replacement (Python `re.sub` template syntax:
     *        `\1`, `\g<name>`, escapes; 0 replaces every match).
     * \param[in] replacement The template.
     * \param[in] text        The subject.
     * \param[in] count       How many matches to replace; 0 for all.
     * \return The result.
     * \throws real::compiled::error On a malformed template or a reference to a group the pattern lacks.
     */
    [[nodiscard]] std::string sub(std::string_view replacement,
                                  std::string_view text,
                                  std::size_t      count = 0) const
    {
      std::string       cause(256, '\0');
      const std::size_t length {real_sub(handle_.get(), text.data(), text.size(), replacement.data(),
                                         replacement.size(), count, nullptr, 0, nullptr, cause.data(), cause.size())};
      if (length == npos) {
        cause.resize(cause.find('\0'));
        throw error(cause, 0, npos);
      }
      std::string out(length, '\0');
      static_cast<void>(real_sub(handle_.get(), text.data(), text.size(), replacement.data(), replacement.size(),
                                 count, out.data(), out.size(), nullptr, cause.data(), cause.size()));
      return out;
    }

  private:
    [[nodiscard]] match_result run(std::string_view text,
                            std::size_t      start,
                            std::size_t      end,
                            int              mode) const
    {
      std::vector<std::size_t> spans(2U * slots_);
      const int                found {real_match(handle_.get(), text.data(), text.size(), start,
                                                 end == npos ? text.size() : end, mode, spans.data())};
      if (found < 0) {
        throw error("real::compiled: the library failed during a match", 0, npos);
      }
      return found == 0 ? match_result {} : match_result {text, std::move(spans)};
    }

    std::string                 pattern_;
    std::shared_ptr<real_regex> handle_;
    std::size_t                 slots_ {1};
  };

  /*!
   * \brief Whether the linked library speaks the interface this header was compiled against.
   * \return True when its `real_abi_version()` equals this header's `REAL_ABI_VERSION`.
   */
  [[nodiscard]] inline bool abi_matches() noexcept
  {
    return real_abi_version() == REAL_ABI_VERSION;
  }

} // namespace real::compiled

#endif // REAL_COMPILED_HPP
