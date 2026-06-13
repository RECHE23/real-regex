/*!
 * \file real.hpp
 * \brief The public API: `real::regex`, `real::static_regex` and results.
 *
 * Header-only, C++20, constexpr from end to end. Include this one header.
 */
#ifndef REAL_REAL_HPP
#define REAL_REAL_HPP

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pike.hpp"
#include "program.hpp"
#include "storage.hpp"
#include "utf8.hpp"

namespace real {

/*!
 * \brief The result of a match attempt: success, spans and captures.
 *
 * Group views point into the searched text, which must outlive the result —
 * the rvalue `std::string` overloads on the regex are deleted to catch the
 * common dangling mistake at compile time. Named-group lookups reference the
 * regex's name table, so the regex must outlive the result too.
 *
 * \tparam SlotStorage The capture-slot container (vector- or static-backed),
 *         supplied by the storage policy.
 */
template <typename SlotStorage>
class basic_match_result
{
public:

  //! Constructs an empty (non-matched) result.
  constexpr basic_match_result() = default;

  /*!
   * \brief Constructs a result from raw slots (used internally by the engine).
   * \param[in] text    The searched text (borrowed; must outlive the result).
   * \param[in] slots   Flattened capture slots (byte offsets, npos for unset).
   * \param[in] matched Whether a match occurred.
   * \param[in] pattern The pattern text (for named-group resolution).
   * \param[in] names   The regex's named-group table (borrowed).
   */
  constexpr basic_match_result(std::string_view text, SlotStorage slots, bool matched, std::string_view pattern, std::span<const detail::named_group> names)
    : text_(text),
      slots_(std::move(slots)),
      matched_(matched),
      pattern_(pattern),
      names_(names)
  {}

  //! \return `true` if the attempt matched.
  [[nodiscard]] constexpr bool matched() const { return matched_; }

  //! \return `true` if the attempt matched (explicit bool conversion).
  constexpr explicit operator bool() const { return matched_; }

  //! \return The number of groups, including group 0 (the whole match).
  [[nodiscard]] constexpr std::size_t size() const { return slots_.size() / 2; }

  /*!
   * \brief Start byte offset of a group.
   * \param[in] group Group number (0 = whole match).
   * \return The offset, or \ref real::npos if the group did not participate.
   */
  [[nodiscard]] constexpr std::size_t start(std::size_t group = 0) const
  {
    return matched_ && group < size() ? slots_[2 * group] : npos;
  }

  /*!
   * \brief End byte offset (exclusive) of a group.
   * \param[in] group Group number (0 = whole match).
   * \return The offset, or \ref real::npos if the group did not participate.
   */
  [[nodiscard]] constexpr std::size_t end(std::size_t group = 0) const
  {
    return matched_ && group < size() ? slots_[(2 * group) + 1] : npos;
  }

  /*!
   * \brief View of a group's matched text.
   * \param[in] group Group number (0 = whole match).
   * \return A view into the searched text, empty if the group is unset.
   */
  [[nodiscard]] constexpr std::string_view operator[](std::size_t group) const
  {
    const std::size_t s = start(group);
    return s == npos ? std::string_view {} : text_.substr(s, end(group) - s);
  }

  /*!
   * \brief Resolves a group name to its number.
   * \param[in] name The group name.
   * \return The group number, or \ref real::npos if unknown.
   */
  [[nodiscard]] constexpr std::size_t group_index(std::string_view name) const
  {
    for (const detail::named_group& ng : names_) {
      const auto begin  = static_cast<std::size_t>(ng.begin);
      const auto length = static_cast<std::size_t>(ng.end - ng.begin);
      if (pattern_.substr(begin, length) == name) {
        return static_cast<std::size_t>(ng.group);
      }
    }
    return npos;
  }

  //! \param[in] name Group name. \return Its start offset, or npos if unknown.
  [[nodiscard]] constexpr std::size_t start(std::string_view name) const
  {
    const std::size_t g = group_index(name);
    return g == npos ? npos : start(g);
  }

  //! \param[in] name Group name. \return Its end offset, or npos if unknown.
  [[nodiscard]] constexpr std::size_t end(std::string_view name) const
  {
    const std::size_t g = group_index(name);
    return g == npos ? npos : end(g);
  }

  //! \param[in] name Group name. \return Its matched text, empty if unknown/unset.
  [[nodiscard]] constexpr std::string_view operator[](std::string_view name) const
  {
    const std::size_t g = group_index(name);
    return g == npos ? std::string_view {} : (*this)[g];
  }

private:

  std::string_view                     text_;             //!< The searched text.
  SlotStorage                          slots_;            //!< Flattened capture slots.
  bool                                 matched_ = false;  //!< Whether a match occurred.
  std::string_view                     pattern_;          //!< Pattern text (for named lookups).
  std::span<const detail::named_group> names_;            //!< Borrowed named-group table.
};

//! The result type of the default, runtime-compiled `real::regex`.
using match_result = basic_match_result<std::vector<std::size_t>>;

/*!
 * \brief Forward iterator over the non-overlapping matches in a text.
 *
 * Follows Python's empty-match rules: an empty match is yielded (even right
 * after a non-empty one), then the scan advances by one codepoint. The regex
 * and the text must outlive the iterator. Obtained from \ref basic_match_range.
 *
 * \tparam Storage The regex's storage policy (selects the result/scratch types).
 */
template <typename Storage>
class basic_match_iterator
{
public:

  using value_type      = basic_match_result<typename Storage::slot_storage>; //!< Yielded match type.
  using difference_type = std::ptrdiff_t;                                     //!< Iterator traits.

  //! Constructs the end sentinel.
  constexpr basic_match_iterator() = default;

  /*!
   * \brief Constructs a begin iterator and finds the first match.
   * \param[in] prog    The compiled program to run.
   * \param[in] pattern The pattern text (for named-group resolution).
   * \param[in] text    The text to iterate over (borrowed).
   */
  constexpr basic_match_iterator(detail::program_view prog, std::string_view pattern, std::string_view text)
    : prog_(prog),
      pattern_(pattern),
      text_(text),
      done_(false)
  {
    advance();
  }

  //! \return The current match.
  [[nodiscard]] constexpr const value_type& operator*() const { return current_; }

  //! \return Pointer to the current match.
  [[nodiscard]] constexpr const value_type* operator->() const { return &current_; }

  //! Advances to the next match. \return *this.
  constexpr basic_match_iterator& operator++()
  {
    advance();
    return *this;
  }

  //! Advances to the next match (post-increment; no value returned).
  constexpr void operator++(int) { advance(); }

  //! \param[in] other Another iterator. \return `true` if both denote the same position/end.
  [[nodiscard]] constexpr bool operator==(const basic_match_iterator& other) const
  {
    return done_ == other.done_ && (done_ || pos_ == other.pos_);
  }

private:

  detail::program_view         prog_;                     //!< The program being run.
  std::string_view             pattern_;                  //!< Pattern text (named lookups).
  std::string_view             text_;                     //!< The text being scanned.
  std::size_t                  pos_                 = 0;   //!< Current scan offset.
  std::size_t                  forbid_empty_until_  = 0;   //!< Empty-match guard (see pike.hpp).
  bool                         done_                = true; //!< True once exhausted.
  value_type                   current_;                  //!< The current match.
  typename Storage::state_type state_;                    //!< VM scratch, reused across the walk.

  //! Finds the next match, applying the empty-match advance rules.
  constexpr void advance()
  {
    if (done_ || pos_ > text_.size()) {
      done_ = true;
      return;
    }
    typename Storage::slot_storage slots;
    detail::pike_vm                vm(prog_, state_);
    if (!vm.run(text_, pos_, detail::run_mode::search, slots, forbid_empty_until_)) {
      done_ = true;
      return;
    }
    const std::size_t start = slots[0];
    const std::size_t end   = slots[1];
    current_                = value_type(text_, std::move(slots), true, pattern_, prog_.names);
    pos_                    = end;
    if (end == start) {
      // CPython 3.7+: after an empty match, the next match may start at the
      // same position only if it is non-empty; another empty match there is
      // skipped. Forbid empty matches up to the next character boundary (a
      // codepoint, or one raw byte in binary mode) so the skip stays aligned.
      forbid_empty_until_ = end >= text_.size()
                              ? text_.size() + 1
                              : end + (prog_.byte_mode ? 1 : detail::codepoint_advance(text_, end));
    }
    else {
      forbid_empty_until_ = 0; // non-empty match: no restriction next time
    }
  }
};

/*!
 * \brief A range of matches, returned by `find_iter()` and usable in range-for.
 * \tparam Storage The regex's storage policy.
 */
template <typename Storage>
class basic_match_range
{
public:

  /*!
   * \brief Binds the range to a program and text.
   * \param[in] prog    The compiled program.
   * \param[in] pattern The pattern text (for named-group resolution).
   * \param[in] text    The text to iterate over (borrowed).
   */
  constexpr basic_match_range(detail::program_view prog, std::string_view pattern, std::string_view text)
    : prog_(prog),
      pattern_(pattern),
      text_(text)
  {}

  //! \return An iterator to the first match.
  [[nodiscard]] constexpr basic_match_iterator<Storage> begin() const
  {
    return {prog_, pattern_, text_};
  }

  //! \return The end sentinel.
  [[nodiscard]] constexpr basic_match_iterator<Storage> end() const { return {}; }

private:

  detail::program_view prog_;     //!< The program being run.
  std::string_view     pattern_;  //!< Pattern text (named lookups).
  std::string_view     text_;     //!< The text to iterate.
};

/*!
 * \brief A compiled regular expression, parameterized on its storage policy.
 *
 * `Storage` owns the program; matching allocates only per-run scratch — and
 * nothing at all when the storage is compile-time. Use the \ref real::regex
 * and \ref real::static_regex aliases rather than this template directly.
 *
 * \tparam Storage \ref real::detail::dynamic_storage or
 *         \ref real::detail::static_storage.
 */
template <typename Storage>
class basic_regex
{
public:

  using result_type = basic_match_result<typename Storage::slot_storage>; //!< This regex's match-result type.

  /*!
   * \brief Compiles \p pattern at run time (the `real::regex` constructor).
   * \param[in] pattern The pattern text.
   * \param[in] f       Optional flags (merged with a leading (?ims)).
   * \throws real::regex_error on an invalid or over-limit pattern.
   */
  constexpr explicit basic_regex(std::string_view pattern, flags f = flags::none)
    requires(!Storage::is_compile_time)
    : program_(Storage::compile(pattern, f))
  {}

  //! Default constructor for the stateless compile-time storage (static_regex).
  constexpr basic_regex()
    requires(Storage::is_compile_time)
  = default;

  /*!
   * \brief Match anchored at the start of \p text (Python `re.match`).
   * \param[in] text The subject text (must outlive the result).
   * \return The match result (test with `matched()` / `operator` bool).
   */
  [[nodiscard]] constexpr result_type match(std::string_view text) const
  {
    return run(text, detail::run_mode::prefix);
  }

  /*!
   * \brief Match the entire \p text (Python `re.fullmatch`).
   * \param[in] text The subject text (must outlive the result).
   * \return The match result.
   */
  [[nodiscard]] constexpr result_type fullmatch(std::string_view text) const
  {
    return run(text, detail::run_mode::full);
  }

  /*!
   * \brief Leftmost match anywhere in \p text (Python `re.search`).
   * \param[in] text The subject text (must outlive the result).
   * \return The match result.
   */
  [[nodiscard]] constexpr result_type search(std::string_view text) const
  {
    return run(text, detail::run_mode::search);
  }

  //! `match` overload for string literals. \param[in] text NUL-terminated text. \return The result.
  [[nodiscard]] constexpr result_type match(const char* text) const
  {
    return match(std::string_view(text));
  }

  //! `fullmatch` overload for string literals. \param[in] text NUL-terminated text. \return The result.
  [[nodiscard]] constexpr result_type fullmatch(const char* text) const
  {
    return fullmatch(std::string_view(text));
  }

  //! `search` overload for string literals. \param[in] text NUL-terminated text. \return The result.
  [[nodiscard]] constexpr result_type search(const char* text) const
  {
    return search(std::string_view(text));
  }

  /*!
   * \brief Lazy range over all non-overlapping matches (Python `re.finditer`).
   *
   * Only callable on an lvalue regex: calling on a temporary would dangle in a
   * C++20 range-for (the range initializer's temporaries die before the loop
   * body), so that misuse is a compile error (deleted rvalue overloads).
   *
   * \param[in] text The subject text (must outlive the range).
   * \return A \ref basic_match_range usable directly in a range-for.
   */
  [[nodiscard]] constexpr basic_match_range<Storage> find_iter(std::string_view text) const&
  {
    return {program_.view(), pattern(), text};
  }

  //! `find_iter` overload for string literals. \param[in] text NUL-terminated text. \return The range.
  [[nodiscard]] constexpr basic_match_range<Storage> find_iter(const char* text) const&
  {
    return find_iter(std::string_view(text));
  }

  //! Deleted: `find_iter` on a temporary regex would dangle.
  [[nodiscard]] basic_match_range<Storage> find_iter(std::string_view text) const&& = delete;
  //! Deleted: `find_iter` on a temporary regex would dangle.
  [[nodiscard]] basic_match_range<Storage> find_iter(const char* text) const&&      = delete;

  /*!
   * \brief All matches, eagerly (like Python `re.findall` but full results).
   *
   * Lvalue-only for the same reason as \ref find_iter (results reference this
   * regex's name table).
   *
   * \param[in] text The subject text (must outlive the results).
   * \return A vector of match results.
   */
  [[nodiscard]] constexpr std::vector<result_type> find_all(std::string_view text) const&
  {
    std::vector<result_type> out;
    for (const result_type& m : find_iter(text)) {
      out.push_back(m);
    }
    return out;
  }

  //! `find_all` overload for string literals. \param[in] text NUL-terminated text. \return The results.
  [[nodiscard]] constexpr std::vector<result_type> find_all(const char* text) const&
  {
    return find_all(std::string_view(text));
  }

  //! Deleted: `find_all` on a temporary regex would dangle.
  [[nodiscard]] std::vector<result_type> find_all(std::string_view text) const&& = delete;
  //! Deleted: `find_all` on a temporary regex would dangle.
  [[nodiscard]] std::vector<result_type> find_all(const char* text) const&&      = delete;

  /*!
   * \brief Replaces matches in \p text (Python `re.sub`).
   *
   * The \p replacement may reference groups: `$$` → '$', `$&` or `$0` →
   * whole match, `$1` …, and `${name}`. Returns an owning string, so a
   * temporary \p text is fine here.
   *
   * \param[in] text        The subject text.
   * \param[in] replacement The replacement template.
   * \param[in] max_count   Maximum replacements (0 = all).
   * \return The resulting string.
   * \throws real::regex_error on a malformed group reference in \p replacement.
   */
  [[nodiscard]] constexpr std::string replace(std::string_view text,
                                              std::string_view replacement,
                                              std::size_t      max_count = 0) const
  {
    std::string out;
    std::size_t last = 0;
    std::size_t done = 0;
    for (const result_type& m : find_iter(text)) {
      if (max_count != 0 && done == max_count) {
        break;
      }
      out.append(text.substr(last, m.start() - last));
      expand_replacement(out, m, replacement);
      last = m.end();
      ++done;
    }
    out.append(text.substr(last));
    return out;
  }

  /*!
   * \brief Splits \p text on matches (Python `re.split`).
   *
   * Each capturing group's text is inserted after its split (an unset group
   * yields an empty view, where Python would use `None`).
   *
   * \param[in] text       The subject text (must outlive the returned views).
   * \param[in] max_splits Maximum splits (0 = split everywhere).
   * \return The pieces, with captured separators interleaved.
   */
  [[nodiscard]] constexpr std::vector<std::string_view> split(std::string_view text,
                                                              std::size_t      max_splits = 0) const
  {
    std::vector<std::string_view> out;
    std::size_t                   last = 0;
    std::size_t                   done = 0;
    for (const result_type& m : find_iter(text)) {
      if (max_splits != 0 && done == max_splits) {
        break;
      }
      out.push_back(text.substr(last, m.start() - last));
      for (std::size_t g = 1; g < m.size(); ++g) {
        out.push_back(m[g]);
      }
      last = m.end();
      ++done;
    }
    out.push_back(text.substr(last));
    return out;
  }

  //! `split` overload for string literals. \param[in] text NUL-terminated text. \param[in] max_splits Max splits. \return The pieces.
  [[nodiscard]] constexpr std::vector<std::string_view> split(const char* text,
                                                              std::size_t max_splits = 0) const
  {
    return split(std::string_view(text), max_splits);
  }

  // Searched text must outlive the result: reject temporary std::string.
  [[nodiscard]] result_type                   match(const std::string&& text) const           = delete; //!< Deleted: temporary text would dangle.
  [[nodiscard]] result_type                   fullmatch(const std::string&& text) const       = delete; //!< Deleted: temporary text would dangle.
  [[nodiscard]] result_type                   search(const std::string&& text) const          = delete; //!< Deleted: temporary text would dangle.
  [[nodiscard]] basic_match_range<Storage>    find_iter(const std::string&& text) const&      = delete; //!< Deleted: temporary text would dangle.
  [[nodiscard]] std::vector<result_type>      find_all(const std::string&& text) const&       = delete; //!< Deleted: temporary text would dangle.
  [[nodiscard]] std::vector<std::string_view> split(const std::string&& text,
                                                    std::size_t         max_splits = 0) const = delete; //!< Deleted: temporary text would dangle.

  //! \return The pattern text this regex was compiled from.
  [[nodiscard]] constexpr std::string_view pattern() const { return program_.pattern(); }

  //! \return The effective flags (constructor flags merged with a (?ims) prefix).
  [[nodiscard]] constexpr flags compile_flags() const { return program_.compiled_flags(); }

  //! \return The number of capturing groups (excluding group 0).
  [[nodiscard]] constexpr std::size_t group_count() const
  {
    return (program_.view().slot_count / 2) - 1;
  }

  /*!
   * \brief The raw compiled program, for embedders (advanced).
   *
   * Lets an embedder (e.g. the Python binding) drive `detail::pike_vm` with
   * caller-owned reusable scratch. Valid as long as this regex is alive.
   *
   * \return A non-owning \ref detail::program_view.
   */
  [[nodiscard]] constexpr detail::program_view raw_program() const
  {
    return program_.view();
  }

  /*!
   * \brief Resolves a group name to its number.
   * \param[in] name The group name.
   * \return The group number, or \ref real::npos if unknown.
   */
  [[nodiscard]] constexpr std::size_t group_index(std::string_view name) const
  {
    for (const detail::named_group& ng : program_.view().names) {
      if (name_of(ng) == name) {
        return static_cast<std::size_t>(ng.group);
      }
    }
    return npos;
  }

  /*!
   * \brief All named groups as (name, number) pairs, in declaration order.
   * \return The list of named groups.
   */
  [[nodiscard]] constexpr std::vector<std::pair<std::string_view, std::size_t>>
  named_groups() const
  {
    std::vector<std::pair<std::string_view, std::size_t>> out;
    for (const detail::named_group& ng : program_.view().names) {
      out.emplace_back(name_of(ng), static_cast<std::size_t>(ng.group));
    }
    return out;
  }

private:

  Storage program_;  //!< The storage policy holding the compiled program.

  //! \param[in] ng A named group. \return Its name, sliced from the pattern text.
  [[nodiscard]] constexpr std::string_view name_of(const detail::named_group& ng) const
  {
    return pattern().substr(static_cast<std::size_t>(ng.begin),
                            static_cast<std::size_t>(ng.end - ng.begin));
  }

  /*!
   * \brief Appends \p replacement to \p out, substituting group references.
   *
   * Strict like Python: an invalid or out-of-range reference is an error.
   *
   * \param[in,out] out         The output string to append to.
   * \param[in]     m           The match supplying the captured groups.
   * \param[in]     replacement The replacement template (`$$`, `$&`, `$1`, `${name}`).
   * \throws real::regex_error on a malformed or out-of-range reference.
   */
  constexpr void expand_replacement(std::string& out, const result_type& m, std::string_view replacement) const
  {
    std::size_t i = 0;
    while (i < replacement.size()) {
      const char c = replacement[i];
      if (c != '$') {
        out.push_back(c);
        ++i;
        continue;
      }
      ++i;
      if (i >= replacement.size()) {
        throw regex_error("dangling $ in replacement", i - 1);
      }
      const char d = replacement[i];
      if (d == '$') {
        out.push_back('$');
        ++i;
      }
      else if (d == '&') {
        out.append(m[0]);
        ++i;
      }
      else if (d >= '0' && d <= '9') {
        std::size_t group = 0;
        while (i < replacement.size() && replacement[i] >= '0' &&
               replacement[i] <= '9') {
          group = (group * 10) + static_cast<std::size_t>(replacement[i] - '0');
          ++i;
        }
        if (group >= m.size()) {
          throw regex_error("invalid group reference in replacement", i);
        }
        out.append(m[group]);
      }
      else if (d == '{') {
        const std::size_t name_begin = i + 1;
        std::size_t       j          = name_begin;
        while (j < replacement.size() && replacement[j] != '}') {
          ++j;
        }
        if (j == replacement.size() || j == name_begin) {
          throw regex_error("malformed ${name} in replacement", i);
        }
        const std::size_t group =
          m.group_index(replacement.substr(name_begin, j - name_begin));
        if (group == npos) {
          throw regex_error("unknown group name in replacement", i);
        }
        out.append(m[group]);
        i = j + 1;
      }
      else {
        throw regex_error("invalid $ escape in replacement", i);
      }
    }
  }

  /*!
   * \brief Runs a single match attempt from offset 0 (backs match/search/fullmatch).
   * \param[in] text The subject text.
   * \param[in] mode The anchoring mode.
   * \return The match result.
   */
  [[nodiscard]] constexpr result_type run(std::string_view text,
                                          detail::run_mode mode) const
  {
    typename Storage::state_type   state;
    typename Storage::slot_storage slots;
    const detail::program_view     prog = program_.view();
    detail::pike_vm                vm(prog, state);
    const bool                     matched = vm.run(text, 0, mode, slots);
    return {text, std::move(slots), matched, pattern(), prog.names};
  }
};

//! The runtime-compiled regex type — the primary entry point.
using regex = basic_regex<detail::dynamic_storage>;

/*!
 * \brief A fully compile-time regex.
 *
 * The pattern is parsed, compiled and exactly sized at compile time; matching
 * allocates nothing and also works in a constexpr context. An invalid pattern
 * is a compile error.
 *
 * \tparam Pattern The pattern, as a \ref fixed_string literal.
 * \tparam F       Compilation flags.
 */
template <fixed_string Pattern, flags F = flags::none>
using static_regex = basic_regex<detail::static_storage<Pattern, F>>;

} // namespace real

#endif // REAL_REAL_HPP
