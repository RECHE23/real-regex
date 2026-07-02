/*!
 * \file std_compat.hpp
 * \brief `real::compat` — a `std::regex`-compatible drop-in (`<regex>` surface), char path.
 *
 * The umbrella header: it includes the three parts below; `#include <real/std_compat.hpp>` stays the
 * one public entry point and the whole `real::compat` API is unchanged. The split is purely
 * organizational — `std_compat_core.hpp` (constants, error, backend-routing screens, `basic_regex`),
 * `std_compat_match.hpp` (`sub_match`, `match_results`, the runner, and the `regex_search` /
 * `regex_match` / `regex_replace` free functions), and `std_compat_iter.hpp` (`regex_iterator`,
 * `regex_token_iterator`).
 *
 * Contract: behave identically to `std::regex` (ECMAScript) where `real` can prove it, and
 * fall back to `std::regex` everywhere else — never a silent divergence. A pattern is run on
 * `real` (linear-time, ReDoS-safe) when possible; backreferences, unbounded/oversized
 * lookarounds, POSIX classes, non-ASCII inside `[...]`, and the non-ECMAScript grammars route
 * to `std::regex` via a compile-time screen plus a compile-failure catch.
 *
 * `real` is always built with `flags::bytes | flags::ecma` so its byte-oriented, ECMAScript-`$`,
 * ECMAScript-`.` semantics align with `std::basic_regex<char>` (validated by a differential).
 *
 * Surface: `basic_regex` / `sub_match` / `match_results` / `regex_error`, `regex_search`,
 * `regex_match` (S1), `regex_replace` (S2a), `regex_iterator` / `regex_token_iterator` (S2b),
 * the full `match_flag_type` (S3), and `wregex` + POSIX grammars + `nosubs` (S4). `real` runs only
 * the `char` / default-traits / ECMAScript / every-group path (see `detail::real_eligible`); wide
 * `CharT`, custom traits, POSIX/`collate`, and `nosubs` are always `std`. `regex_replace`/iterators
 * route nullable patterns to `std::regex` (the empty-match traversal differs from ECMAScript, see
 * `basic_regex::nullable`), and a constraining `match_flag` routes that operation to `std`.
 */
#ifndef REAL_STD_COMPAT_HPP
#define REAL_STD_COMPAT_HPP

#include "std_compat_core.hpp"
#include "std_compat_match.hpp"
#include "std_compat_iter.hpp"

#endif // REAL_STD_COMPAT_HPP
