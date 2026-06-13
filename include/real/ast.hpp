/*!
 * \file ast.hpp
 * \brief Pattern text → AST, via a constexpr recursive-descent parser.
 *
 * The parser builds nodes in an index-based pool (no pointers, so it is
 * constexpr-friendly). It accepts only the syntax the rest of the pipeline
 * implements; everything else is a \ref real::regex_error.
 *
 * Character classes are ASCII-only by design (non-ASCII members are
 * rejected): this guarantees every construct consumes whole codepoints, so
 * match boundaries never split a UTF-8 sequence. Negated classes and `.`
 * match any non-ASCII codepoint, like Python's `re` on ASCII classes.
 */
#ifndef REAL_AST_HPP
#define REAL_AST_HPP

#include <cstdint>
#include <string_view>
#include <vector>

#include "charclass.hpp"
#include "config.hpp"
#include "program.hpp"

namespace real::detail {

//! Kind of an AST node; selects which fields of \ref ast_node are meaningful.
enum class node_kind : std::uint8_t
{
  empty,       //!< Matches the empty string.
  byte,        //!< One exact byte.
  klass,       //!< One codepoint constrained by classes[klass] (negated or not).
  any,         //!< One codepoint, except newline (the `.` metacharacter).
  concat,      //!< Children matched in sequence.
  repeat,      //!< Child repeated `[min, max]` times (max -1 = unbounded).
  alternation, //!< Children are branches, leftmost preferred.
  group,       //!< Child wrapped in a group; `group` >= 0 when capturing.
  anchor,      //!< Zero-width assertion; kind in \ref ast_node::anchor.
};

//! The specific zero-width assertion of an \ref node_kind::anchor node.
enum class anchor_kind : std::uint8_t
{
  caret,             //!< `^`  (text or line start, depending on multiline).
  dollar,            //!< `$`  (end, before a trailing `\n`, or line end with m).
  text_start,        //!< `\A`.
  text_end,          //!< `\Z`.
  word_boundary,     //!< `\b`.
  not_word_boundary, //!< `\B`.
  word_start,        //!< `\<` (start of word; REAL extension, not in Python re).
  word_end,          //!< `\>` (end of word; REAL extension, not in Python re).
};

//! One AST node. Active fields depend on \ref kind (noted per field).
struct ast_node
{
  node_kind    kind {node_kind::empty};     //!< Which fields below are meaningful.
  std::uint8_t byte {};                     //!< byte: the exact byte value.
  anchor_kind  anchor {anchor_kind::caret}; //!< anchor: the assertion kind.
  bool         negated {};                  //!< klass: written as `[^...]` / `\D` `\W` `\S`.
  bool         lazy {};                     //!< repeat: prefer the shortest expansion.
  std::int32_t klass {-1};                  //!< klass: index into \ref ast::classes.
  std::int32_t min {};                      //!< repeat: minimum count.
  std::int32_t max {-1};                    //!< repeat: maximum count (-1 = unbounded).
  std::int32_t group {-1};                  //!< group: capture number, -1 for `(?:...)`.
  std::int32_t child {-1};                  //!< First child (concat, repeat, alternation, group).
  std::int32_t next {-1};                   //!< Next sibling in the parent's child list.
};

/*!
 * \brief A parsed pattern: the node pool plus side tables.
 *
 * Resource caps used during parsing and later Thompson unrolling are
 * centralized in config.hpp (\ref max_repeat_count, \ref max_group_count,
 * \ref max_nesting_depth, \ref max_program_size).
 */
struct ast
{
  std::vector<ast_node>    nodes;                      //!< The node pool; \ref root indexes it.
  std::vector<char_class>  classes;                    //!< Class bitmaps as written, before negation.
  std::vector<named_group> names;                      //!< Named capture groups.
  flags                    inline_flags {flags::none}; //!< Flags from a leading `(?ims)`.
  std::int32_t             group_count {};             //!< Number of capturing groups.
  std::int32_t             root {-1};                  //!< Index of the root node.
};

//! Recursive-descent parser: a pattern string in, an \ref ast out.
class parser
{
public:

  //! \param[in] pattern The pattern text to parse (borrowed, must outlive use).
  constexpr explicit parser(std::string_view pattern)
    : pattern_(pattern)
  {}

  /*!
   * \brief Parses the whole pattern.
   * \return The resulting \ref ast.
   * \throws real::regex_error on any unsupported or malformed syntax.
   */
  constexpr ast parse()
  {
    ast out;
    while (parse_global_flags_prefix(out)) {
    }
    out.root = parse_alternation(out);
    if (pos_ != pattern_.size()) {
      fail("unbalanced parenthesis"); // only a stray ')' stops earlier
    }
    return out;
  }

private:

  std::string_view pattern_;  //!< The pattern being parsed.
  std::size_t      pos_ {};   //!< Current read offset into \ref pattern_.
  std::int32_t     depth_ {}; //!< Current group nesting (see \ref max_nesting_depth).

  /*!
   * \brief Aborts the parse with a \ref real::regex_error at the current offset.
   *
   * A template so the always-throwing body stays legal inside a constexpr
   * function (the ill-formed, no-diagnostic-required rule does not apply to
   * templates); during constant evaluation the throw fails compilation with
   * \p message in the diagnostic trace.
   *
   * \tparam Error The exception type to throw (defaults to regex_error).
   * \param[in] message The cause, shown in the error and the constexpr trace.
   */
  template <typename Error = regex_error>
  [[noreturn]] constexpr void fail(const char* message) const
  {
    throw Error(message, pos_);
  }

  //! \return `true` if the read offset is at or past the end of the pattern.
  [[nodiscard]] constexpr bool eof() const { return pos_ >= pattern_.size(); }

  //! \return The current character without consuming it (undefined at eof()).
  [[nodiscard]] constexpr char peek() const { return pattern_[pos_]; }

  /*!
   * \brief Consumes the current character if it equals \p c.
   * \param[in] c The character to match.
   * \return `true` (and advances) on a match, else `false`.
   */
  [[nodiscard]] constexpr bool accept(char c)
  {
    if (!eof() && peek() == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  //! \param[in] c A character. \return `true` if \p c is in `[0-9A-Za-z]`.
  static constexpr bool is_ascii_alnum(char c)
  {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }

  /*!
   * \brief Appends \p node to the pool.
   * \param[in,out] out  The AST being built.
   * \param[in]     node The node to append.
   * \return The index of the appended node.
   */
  static constexpr std::int32_t add_node(ast& out, ast_node node)
  {
    out.nodes.push_back(node);
    return static_cast<std::int32_t>(out.nodes.size()) - 1;
  }

  /*!
   * \brief Interns a class bitmap and appends a \ref node_kind::klass node.
   * \param[in,out] out     The AST being built.
   * \param[in]     cc      The class bitmap as written (before negation).
   * \param[in]     negated Whether the class was written negated.
   * \return The index of the new node.
   */
  static constexpr std::int32_t add_class_node(ast& out, const char_class& cc, bool negated)
  {
    out.classes.push_back(cc);
    const auto index {static_cast<std::int32_t>(out.classes.size()) - 1};
    return add_node(out, {.kind = node_kind::klass, .negated = negated, .klass = index});
  }

  /*!
   * \brief Parses `alternation := sequence ('|' sequence)*`.
   *
   * The leftmost branch is preferred (Python / Perl semantics, not longest).
   *
   * \param[in,out] out The AST being built.
   * \return The index of the resulting node (a branch, or a bare sequence).
   */
  constexpr std::int32_t parse_alternation(ast& out)
  {
    std::int32_t first {parse_sequence(out)};
    if (eof() || peek() != '|') {
      return first;
    }
    std::int32_t last {first};
    while (accept('|')) {
      const std::int32_t branch {parse_sequence(out)}; // may be empty
      out.nodes[static_cast<std::size_t>(last)].next = branch;
      last                                           = branch;
    }
    const std::int32_t alt                         = add_node(out, {.kind = node_kind::alternation});
    out.nodes[static_cast<std::size_t>(alt)].child = first;
    return alt;
  }

  /*!
   * \brief Parses `sequence := (atom quantifier?)*`, stopping at `|` or `)`.
   * \param[in,out] out The AST being built.
   * \return The index of a concat node, a single atom, or an empty node.
   */
  constexpr std::int32_t parse_sequence(ast& out)
  {
    std::int32_t first {-1};
    std::int32_t last {-1};
    while (!eof() && peek() != '|' && peek() != ')') {
      std::int32_t atom {parse_atom(out)};
      atom = parse_quantifier(out, atom);
      if (first == -1) {
        first = atom;
      }
      else {
        out.nodes[static_cast<std::size_t>(last)].next = atom;
      }
      last = atom;
    }
    if (first == -1) {
      return add_node(out, {.kind = node_kind::empty});
    }
    if (out.nodes[static_cast<std::size_t>(first)].next == -1) {
      return first; // single atom: no concat wrapper needed
    }
    const std::int32_t seq                         = add_node(out, {.kind = node_kind::concat});
    out.nodes[static_cast<std::size_t>(seq)].child = first;
    return seq;
  }

  /*!
   * \brief Parses one atom: a literal, `.`, a class, a group, an anchor or an escape.
   * \param[in,out] out The AST being built.
   * \return The index of the atom node.
   */
  constexpr std::int32_t parse_atom(ast& out)
  {
    const char c {peek()};
    switch (c) {
      case '*':
      case '+':
      case '?':
        fail("nothing to repeat");
      case '^':
        ++pos_;
        return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::caret});
      case '$':
        ++pos_;
        return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::dollar});
      case '(':
        return parse_group(out);
      case ')':
        fail("unbalanced parenthesis");
      case '.':
        ++pos_;
        return add_node(out, {.kind = node_kind::any});
      case '[':
        return parse_class(out);
      case '\\':
        return parse_escape(out);
      default:
        // Like Python: lone '{', ']' and '}' are ordinary characters.
        ++pos_;
        return add_node(out, {.kind = node_kind::byte, .byte = static_cast<std::uint8_t>(c)});
    }
  }

  /*!
   * \brief Wraps \p atom in a repeat node if a quantifier follows.
   *
   * Grammar: `quantifier := ('*' | '+' | '?' | '{n}' | '{n,}' | '{,m}' |
   * '{n,m}') '?'?`. An invalid `{...}` is not a quantifier at all
   * and stays literal text, exactly like Python (e.g. `a{`, `a{2,3x`,
   * `a{,}` all match literally). A bare anchor cannot be repeated.
   *
   * \param[in,out] out  The AST being built.
   * \param[in]     atom Index of the atom the quantifier would apply to.
   * \return The repeat node index, or \p atom unchanged if no quantifier.
   */
  constexpr std::int32_t parse_quantifier(ast& out, std::int32_t atom)
  {
    if (eof()) {
      return atom;
    }
    // Like Python: a bare anchor cannot be repeated ((?:^)* is fine).
    if (out.nodes[static_cast<std::size_t>(atom)].kind == node_kind::anchor &&
        (peek() == '*' || peek() == '+' || peek() == '?' || peek() == '{')) {
      std::int32_t ignored_min {};
      std::int32_t ignored_max {-1};
      if (peek() != '{' || try_parse_braces(ignored_min, ignored_max)) {
        fail("nothing to repeat");
      }
    }
    std::int32_t min {};
    std::int32_t max {-1};
    switch (peek()) {
      case '*':
        ++pos_;
        break;
      case '+':
        ++pos_;
        min = 1;
        break;
      case '?':
        ++pos_;
        max = 1;
        break;
      case '{':
        if (!try_parse_braces(min, max)) {
          return atom; // literal '{': handled as the next atom
        }
        break;
      default:
        return atom;
    }
    const bool lazy {accept('?')};
    if (!eof()) {
      const char   c {peek()};
      std::int32_t ignored_min {};
      std::int32_t ignored_max {-1};
      if (c == '*' || c == '+' || c == '?' ||
          (c == '{' && try_parse_braces(ignored_min, ignored_max))) {
        fail("multiple repeat");
      }
    }
    return add_node(out, {.kind = node_kind::repeat, .lazy = lazy, .min = min, .max = max, .child = atom});
  }

  /*!
   * \brief Tries to parse `{n} / {n,} / {,m} / {n,m}` starting at `{`.
   * \param[out] min Lower bound on success.
   * \param[out] max Upper bound on success (-1 for unbounded).
   * \return `true` on a valid quantifier (position advanced); `false` if the
   *         braces are not a quantifier (position restored — literal text).
   * \throws real::regex_error when the bounds are impossible (min > max).
   */
  constexpr bool try_parse_braces(std::int32_t& min, std::int32_t& max)
  {
    const std::size_t save {pos_};
    ++pos_; // consume '{'
    const std::int32_t n {parse_repeat_count()};
    std::int32_t       m {n};
    bool               has_comma {};
    if (accept(',')) {
      has_comma = true;
      m         = parse_repeat_count();
    }
    if (!accept('}') || (n < 0 && m < 0)) {
      pos_ = save;
      return false; // "{", "{}", "{,}", "{x"…: literal text
    }
    min = n < 0 ? 0 : n;
    max = (has_comma && m < 0) ? -1 : m;
    if (max != -1 && max < min) {
      pos_ = save;
      fail("min repeat greater than max repeat");
    }
    return true;
  }

  /*!
   * \brief Reads an optional decimal repeat count.
   * \return The count, or -1 when no digits are present.
   * \throws real::regex_error if the count exceeds \ref max_repeat_count
   *         (counted repetitions are compiled by unrolling, so they are capped).
   */
  constexpr std::int32_t parse_repeat_count()
  {
    std::int32_t value {-1};
    while (!eof() && peek() >= '0' && peek() <= '9') {
      value = value < 0 ? 0 : value;
      value = (value * 10) + (peek() - '0');
      if (value > max_repeat_count) {
        fail("repetition count too large");
      }
      ++pos_;
    }
    return value;
  }

  /*!
   * \brief Consumes \p c or fails.
   * \param[in] c       The required character.
   * \param[in] message Error message if \p c is not present.
   * \throws real::regex_error when the next character is not \p c.
   */
  constexpr void expect(char c, const char* message)
  {
    if (!accept(c)) {
      fail(message);
    }
  }

  /*!
   * \brief Maps a flag letter to its \ref flags value.
   * \param[in] c One of 'i', 'm', 's', 'a'.
   * \return The flag; \ref flags::none for 'a' (ASCII — already the default)
   *         and for any unrecognized letter.
   */
  static constexpr flags flag_for_letter(char c)
  {
    switch (c) {
      case 'i':
        return flags::icase;
      case 'm':
        return flags::multiline;
      case 's':
        return flags::dotall;
      // 'a' (ASCII) is a recognized flag, accepted as a no-op because ASCII
      // is already this library's semantics — intent distinct from an
      // unrecognized letter, hence kept separate from default.
      case 'a': // NOLINT(bugprone-branch-clone)
        return flags::none;
      default:
        return flags::none;
    }
  }

  //! \param[in] c A character. \return `true` if \p c is a flag letter (imsa).
  static constexpr bool is_flag_letter(char c)
  {
    return c == 'i' || c == 'm' || c == 's' || c == 'a';
  }

  /*!
   * \brief Consumes a leading `(?ims)` global-flags group, if present.
   *
   * Like Python (3.11+), global flags are only legal at the very start of the
   * pattern; later occurrences are rejected in \ref parse_group.
   *
   * \param[in,out] out Receives the flags into \ref ast::inline_flags.
   * \return `true` if a flags group was consumed (position advanced), else
   *         `false` (position restored, for \ref parse_group to handle).
   */
  constexpr bool parse_global_flags_prefix(ast& out)
  {
    const std::size_t save {pos_};
    if (!accept('(') || !accept('?')) {
      pos_ = save;
      return false;
    }
    flags found {flags::none};
    bool  any_letter {};
    while (!eof() && is_flag_letter(peek())) {
      found      = found | flag_for_letter(peek());
      any_letter = true;
      ++pos_;
    }
    if (!any_letter || !accept(')')) {
      pos_ = save; // some other (?...) construct: let parse_group decide
      return false;
    }
    out.inline_flags = out.inline_flags | found;
    return true;
  }

  /*!
   * \brief Parses a group construct.
   *
   * Grammar:
   * \code
   * group := '(' alternation ')'           capturing, numbered by '('
   *        | '(?:' alternation ')'         non-capturing
   *        | '(?P<name>' alternation ')'   named (Python style)
   *        | '(?<name>'  alternation ')'   named (.NET style)
   * \endcode
   * Unsupported extensions (lookaround, backreferences, atomic groups,
   * scoped inline flags) fail with a message naming the feature. Nesting
   * beyond \ref max_nesting_depth is rejected.
   *
   * \param[in,out] out The AST being built.
   * \return The index of the \ref node_kind::group node.
   * \throws real::regex_error on an unterminated or unsupported group.
   */
  constexpr std::int32_t parse_group(ast& out)
  {
    const std::size_t open_pos {pos_};
    if (++depth_ > max_nesting_depth) {
      fail("pattern nesting too deep");
    }
    ++pos_; // consume '('
    std::int32_t group {-1};
    if (accept('?')) {
      if (accept(':')) {
        // non-capturing
      }
      else if (accept('P')) {
        if (accept('<')) {
          group = new_group(out, open_pos);
          parse_group_name(out, group);
        }
        else if (!eof() && peek() == '=') {
          fail("backreferences are not supported");
        }
        else {
          fail("unknown extension");
        }
      }
      else if (accept('<')) {
        if (!eof() && (peek() == '=' || peek() == '!')) {
          fail("lookbehind is not supported");
        }
        group = new_group(out, open_pos);
        parse_group_name(out, group);
      }
      else if (!eof() && (peek() == '=' || peek() == '!')) {
        fail("lookahead is not supported");
      }
      else if (!eof() && peek() == '>') {
        fail("atomic groups are not supported");
      }
      else if (!eof() && is_flag_letter(peek())) {
        while (!eof() && is_flag_letter(peek())) {
          ++pos_;
        }
        if (!eof() && peek() == ':') {
          fail("scoped inline flags are not supported");
        }
        fail("global flags not at the start of the expression");
      }
      else {
        fail("unknown extension");
      }
    }
    else {
      group = new_group(out, open_pos);
    }
    const std::int32_t body {parse_alternation(out)};
    if (!accept(')')) {
      pos_ = open_pos;
      fail("missing ), unterminated subpattern");
    }
    --depth_;
    return add_node(out, {.kind = node_kind::group, .group = group, .child = body});
  }

  /*!
   * \brief Allocates the next capture group number.
   * \param[in,out] out      The AST being built.
   * \param[in]     open_pos Offset of the group's `(` (for error reporting).
   * \return The new (1-based) capture group number.
   * \throws real::regex_error beyond \ref max_group_count.
   */
  constexpr std::int32_t new_group(ast& out, std::size_t open_pos)
  {
    if (out.group_count >= max_group_count) {
      pos_ = open_pos;
      fail("too many capture groups");
    }
    return ++out.group_count;
  }

  //! \param[in] c A character. \return `true` if \p c may start a group name.
  static constexpr bool is_name_start(char c)
  {
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }

  /*!
   * \brief Parses `name := [A-Za-z_][A-Za-z0-9_]* '>'` and records it.
   * \param[in,out] out   The AST; the name is appended to \ref ast::names.
   * \param[in]     group The capture number this name refers to.
   * \throws real::regex_error on a bad character or a duplicate name.
   */
  constexpr void parse_group_name(ast& out, std::int32_t group)
  {
    const std::size_t begin {pos_};
    if (eof() || !is_name_start(peek())) {
      fail("bad character in group name");
    }
    while (!eof() && (is_ascii_alnum(peek()) || peek() == '_')) {
      ++pos_;
    }
    const std::size_t end {pos_};
    expect('>', "bad character in group name");
    for (const named_group& existing : out.names) {
      const std::string_view name {pattern_.substr(begin, end - begin)};
      const auto             e_begin {static_cast<std::size_t>(existing.begin)};
      const auto             e_end {static_cast<std::size_t>(existing.end)};
      if (pattern_.substr(e_begin, e_end - e_begin) == name) {
        fail("redefinition of group name");
      }
    }
    out.names.push_back({.group = group,
                         .begin = static_cast<std::int32_t>(begin),
                         .end   = static_cast<std::int32_t>(end)});
  }

  /*!
   * \brief Parses a single-byte escape (valid inside and outside classes).
   *
   * Handles `\n` `\t` `\r` `\f` `\v` `\a` `\0`, `\xHH` and
   * escaped ASCII punctuation.
   *
   * \return The byte value, or -1 when the escape is not a single byte
   *         (the caller then handles `\d` `\w` `\s`, etc.).
   * \throws real::regex_error on a malformed `\x` escape.
   */
  constexpr std::int32_t parse_byte_escape()
  {
    const char c {peek()};
    switch (c) {
      case 'n':
        ++pos_;
        return '\n';
      case 't':
        ++pos_;
        return '\t';
      case 'r':
        ++pos_;
        return '\r';
      case 'f':
        ++pos_;
        return '\f';
      case 'v':
        ++pos_;
        return '\v';
      case 'a':
        ++pos_;
        return '\a';
      case '0':
        ++pos_;
        return '\0';
      case 'x':
        {
          ++pos_;
          const std::int32_t hi {hex_digit()};
          const std::int32_t lo {hex_digit()};
          return (hi * 16) + lo; // arithmetic, not signed bitwise (MISRA)
        }
      default:
        // Any escaped ASCII punctuation is that literal character.
        if (static_cast<std::uint8_t>(c) < 0x80 && !is_ascii_alnum(c)) {
          ++pos_;
          return static_cast<std::uint8_t>(c);
        }
        return -1;
    }
  }

  /*!
   * \brief Consumes one hexadecimal digit.
   * \return Its value in `[0, 15]`.
   * \throws real::regex_error if the next character is not a hex digit.
   */
  constexpr std::int32_t hex_digit()
  {
    if (eof()) {
      fail("invalid \\x escape: expected two hex digits");
    }
    const char c {peek()};
    ++pos_;
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    --pos_;
    fail("invalid \\x escape: expected two hex digits");
  }

  /*!
   * \brief Parses an escape outside a character class.
   *
   * Handles the class escapes `\d` `\D` `\w` `\W` `\s` `\S`, the
   * anchors `\A` `\Z` `\b` `\B`, and single-byte escapes.
   *
   * \param[in,out] out The AST being built.
   * \return The index of the resulting node.
   * \throws real::regex_error on a dangling or unsupported escape.
   */
  constexpr std::int32_t parse_escape(ast& out)
  {
    ++pos_; // consume the backslash
    if (eof()) {
      fail("dangling backslash");
    }
    switch (peek()) {
      case 'd':
        ++pos_;
        return add_class_node(out, digit_set(), false);
      case 'D':
        ++pos_;
        return add_class_node(out, digit_set(), true);
      case 'w':
        ++pos_;
        return add_class_node(out, word_set(), false);
      case 'W':
        ++pos_;
        return add_class_node(out, word_set(), true);
      case 's':
        ++pos_;
        return add_class_node(out, space_set(), false);
      case 'S':
        ++pos_;
        return add_class_node(out, space_set(), true);
      case 'A':
        ++pos_;
        return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::text_start});
      case 'Z':
        ++pos_;
        return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::text_end});
      case 'b':
        ++pos_;
        return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::word_boundary});
      case 'B':
        ++pos_;
        return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::not_word_boundary});
      case '<':
        ++pos_;
        return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::word_start});
      case '>':
        ++pos_;
        return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::word_end});
      default:
        {
          const std::int32_t b {parse_byte_escape()};
          if (b < 0) {
            fail("unsupported escape sequence");
          }
          return add_node(out, {.kind = node_kind::byte, .byte = static_cast<std::uint8_t>(b)});
        }
    }
  }

  /*!
   * \brief Parses one member inside a character class.
   * \param[in,out] cc The class being built; a set member (`\d` etc.) is
   *                   merged directly into it.
   * \return A single byte (usable as a range endpoint), or -1 when the member
   *         was a whole set merged into \p cc.
   * \throws real::regex_error on a non-ASCII member or an unsupported escape.
   */
  constexpr std::int32_t parse_class_item(char_class& cc)
  {
    const char c {peek()};
    if (static_cast<std::uint8_t>(c) >= 0x80) {
      fail("non-ASCII character class member not supported");
    }
    if (c != '\\') {
      ++pos_;
      return static_cast<std::uint8_t>(c);
    }
    ++pos_; // consume the backslash
    if (eof()) {
      fail("dangling backslash");
    }
    switch (peek()) {
      case 'd':
        ++pos_;
        cc.merge(digit_set());
        return -1;
      case 'w':
        ++pos_;
        cc.merge(word_set());
        return -1;
      case 's':
        ++pos_;
        cc.merge(space_set());
        return -1;
      case 'D':
      case 'W':
      case 'S':
        fail("complemented set not supported inside a character class");
      case 'b':
        ++pos_;
        return 0x08; // backspace, only inside classes
      default:
        {
          const std::int32_t b {parse_byte_escape()};
          if (b < 0) {
            fail("unsupported escape sequence");
          }
          return b;
        }
    }
  }

  /*!
   * \brief Parses a bracketed character class `[...]` or `[^...]`.
   *
   * Supports ranges, escapes and the embedded set escapes; a `]` right after
   * `[` or `[^` is a literal, and a trailing `-` is a literal dash.
   *
   * \param[in,out] out The AST being built.
   * \return The index of the \ref node_kind::klass node.
   * \throws real::regex_error on an unterminated class or a bad range.
   */
  constexpr std::int32_t parse_class(ast& out)
  {
    const std::size_t open_pos {pos_};
    ++pos_; // consume '['
    const bool negated {accept('^')};
    char_class cc;
    bool       first {true};
    while (true) {
      if (eof()) {
        pos_ = open_pos;
        fail("unterminated character class");
      }
      if (peek() == ']' && !first) {
        ++pos_;
        break;
      }
      first = false; // a ']' right after '[' or '[^' is a literal
      const std::size_t  item_pos {pos_};
      const std::int32_t lo {parse_class_item(cc)};
      if (lo < 0) {
        continue; // set item: nothing more to do
      }
      // Possible range: 'x-y', where a trailing '-]' is a literal '-'.
      if (!eof() && peek() == '-' && pos_ + 1 < pattern_.size() &&
          pattern_[pos_ + 1] != ']') {
        ++pos_; // consume '-'
        const std::int32_t hi {parse_class_item(cc)};
        if (hi < 0 || hi < lo) {
          pos_ = item_pos;
          fail("bad character range");
        }
        cc.set_range(static_cast<std::uint8_t>(lo), static_cast<std::uint8_t>(hi));
      }
      else {
        cc.set(static_cast<std::uint8_t>(lo));
      }
    }
    return add_class_node(out, cc, negated);
  }
};

/*!
 * \brief Parses \p pattern into an \ref ast (convenience over \ref parser).
 * \param[in] pattern The pattern text.
 * \return The parsed AST.
 * \throws real::regex_error on unsupported or malformed syntax.
 */
constexpr ast parse(std::string_view pattern)
{
  return parser(pattern).parse();
}

} // namespace real::detail

#endif // REAL_AST_HPP
