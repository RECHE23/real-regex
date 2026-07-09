/*!
 * \file regex_set.hpp
 * \brief `real::regex_set` — multi-pattern which-matched set (Stage-1 MVP).
 *
 * A set of patterns answered with **which-matched** semantics (RE2::Set / rust
 * `RegexSet`): which members match the subject at least once. This is **not**
 * \ref real::dfa (maximal-munch lexer: one winner at the cursor). The MVP is
 * N independent walks with per-pattern early-exit — never N × count_matches and
 * never `real::dfa::match`.
 *
 * Include this header explicitly; \c real.hpp does not pull it in (same opt-in
 * style as \c dfa.hpp).
 */
#ifndef REAL_REGEX_SET_HPP
#define REAL_REGEX_SET_HPP

#include "real/real.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace real {

  /*!
   * \brief Multi-pattern set: which patterns match the subject at least once.
   *
   * Construction compiles every pattern (same \ref flags as \ref regex). If any
   * pattern is invalid or unsupported, the constructor throws \ref regex_error —
   * there is no silent skip. Capture groups are not reported by the set; re-run
   * the individual \ref regex if groups are needed.
   *
   * Bitset order is the construction order: index 0 is the first pattern, etc.
   *
   * \note Stage-1 implementation: N independent \ref regex::search walks with
   *       per-pattern early-exit. A future fused single-pass may replace the
   *       walk without changing this public contract.
   */
  class regex_set
  {
  public:

    /*!
     * \brief Compiles every pattern in \p patterns (construction order = bitset order).
     * \param[in] patterns Pattern texts; must be non-empty for a useful set (empty is allowed).
     * \param[in] compile_flags Flags applied to every pattern (same as \ref regex).
     * \throws regex_error if any pattern fails to compile.
     */
    explicit regex_set(std::span<const std::string_view> patterns,
                       flags                             compile_flags = flags::none)
      : flags_(compile_flags)
    {
      members_.reserve(patterns.size());
      for (const std::string_view pat : patterns) {
        members_.emplace_back(pat, compile_flags); // throws regex_error on failure
      }
    }

    /*!
     * \brief Convenience: compile from a contiguous array of string views.
     * \param[in] patterns Pattern texts.
     * \param[in] n        Number of patterns.
     * \param[in] compile_flags Flags applied to every pattern.
     * \throws regex_error if any pattern fails to compile.
     */
    regex_set(const std::string_view* patterns,
              std::size_t             n,
              flags                   compile_flags = flags::none)
      : regex_set(std::span<const std::string_view> {patterns, n},
                  compile_flags)
    {}

    /*!
     * \brief Brace-init / inline list: \c regex_set{"a", "b", R"(\\d+)"} .
     *
     * String literals convert to \c string_view; construction order = bitset order.
     * \param[in] patterns Pattern texts.
     * \param[in] compile_flags Flags applied to every pattern.
     * \throws regex_error if any pattern fails to compile.
     */
    regex_set(std::initializer_list<std::string_view> patterns,
              flags                                   compile_flags = flags::none)
      : regex_set(std::span<const std::string_view> {patterns.begin(), patterns.size()},
                  compile_flags)
    {}

    /*!
     * \brief Compile from owning strings (e.g. \c std::vector<std::string>).
     * \param[in] patterns Pattern texts (views borrowed only during construction).
     * \param[in] compile_flags Flags applied to every pattern.
     * \throws regex_error if any pattern fails to compile.
     */
    explicit regex_set(std::span<const std::string> patterns,
                       flags                        compile_flags = flags::none)
      : flags_(compile_flags)
    {
      members_.reserve(patterns.size());
      for (const std::string& pat : patterns) {
        members_.emplace_back(pat, compile_flags);
      }
    }

    //! \brief Number of patterns in the set (bitset length).
    [[nodiscard]] std::size_t size() const noexcept
    {
      return members_.size();
    }

    //! \brief True if the set has no patterns.
    [[nodiscard]] bool empty() const noexcept
    {
      return members_.empty();
    }

    //! \brief Compilation flags shared by every member.
    [[nodiscard]] flags compile_flags() const noexcept
    {
      return flags_;
    }

    /*!
     * \brief True if **any** pattern matches the subject at least once.
     *
     * Stops at the first matching pattern (any-match early exit). Region
     * semantics match \ref regex::search — \p endpos truncates the view; \p pos
     * is the start offset (not a slice).
     *
     * \param[in] text   Subject text.
     * \param[in] pos    Byte start offset.
     * \param[in] endpos Exclusive end; \ref npos = end of text.
     */
    [[nodiscard]] bool is_match(std::string_view text,
                                std::size_t      pos    = 0,
                                std::size_t      endpos = npos) const
    {
      for (const regex& re : members_) {
        if (re.search(text, pos, endpos)) {
          return true;
        }
      }
      return false;
    }

    /*!
     * \brief Which patterns match at least once (construction-order bitset).
     *
     * For each pattern, \ref regex::search runs until the first match of that
     * pattern (per-pattern early exit), then the next pattern. Never full-scan
     * counts. Index \c i is true iff pattern \c i matched.
     *
     * \param[in] text   Subject text.
     * \param[in] pos    Byte start offset.
     * \param[in] endpos Exclusive end; \ref npos = end of text.
     * \return A vector of length \ref size(); order = construction order.
     */
    [[nodiscard]] std::vector<bool> matches(std::string_view text,
                                            std::size_t      pos    = 0,
                                            std::size_t      endpos = npos) const
    {
      std::vector<bool> hit(members_.size(), false);
      for (std::size_t i = 0; i < members_.size(); ++i) {
        if (members_[i].search(text, pos, endpos)) {
          hit[i] = true;
        }
      }
      return hit;
    }

    /*!
     * \brief Indices of patterns that match (construction order, ascending).
     *
     * Equivalent to collecting every \c i where \ref matches is true.
     */
    [[nodiscard]] std::vector<std::size_t> which(std::string_view text,
                                                 std::size_t      pos    = 0,
                                                 std::size_t      endpos = npos) const
    {
      std::vector<std::size_t> ids;
      ids.reserve(members_.size());
      for (std::size_t i = 0; i < members_.size(); ++i) {
        if (members_[i].search(text, pos, endpos)) {
          ids.push_back(i);
        }
      }
      return ids;
    }

    /*!
     * \brief Access the compiled pattern at construction index \p i.
     * \param[in] i Pattern index in `[0, size())`.
     * \return The member regex (for capture re-runs after which-matched).
     */
    [[nodiscard]] const regex& operator[](std::size_t i) const
    {
      return members_.at(i);
    }

  private:

    std::vector<regex> members_;
    flags              flags_ {flags::none};
  };
} // namespace real

#endif // REAL_REGEX_SET_HPP
