// Pattern composer shared by the route and allocation probes.
//
// Extracted so the two probes cannot drift: a route table and an allocation table describing
// DIFFERENT pattern populations would be quietly incomparable, and the whole point of pairing them
// is that a route's allocation budget is a property of the route.
//
// The composer is deliberately part seeded and part free. Free composition alone put 3354 of 3407
// patterns on `general_full` -- every fast route wants a WHOLE-PATTERN shape and almost any added
// fragment breaks it -- so canonical seeds and one-edit mutations around them carry half the draw,
// and free composition keeps the rest because it is what finds shapes nobody thought to seed.

#ifndef REAL_BENCH_PATTERN_GEN_HPP
#define REAL_BENCH_PATTERN_GEN_HPP

#include <random>
#include <string>
#include <vector>

namespace bench_gen {

  //! Feature fragments. Composition is what finds route boundaries: nearly every surprise this
  //! engine has produced came from one edit flipping which route a pattern takes.
  const std::vector<std::string> k_atoms {
    "a",     "abc",   "xyzzy",                       // literals: exact_literal / inner_literal
    "[a-z]", "[0-9]", "[^,]",     "[a-f0-9]",        // byte classes: class_loop
    "\\w",   "\\d",   "\\s",                         // text-mode shorthands: cp_class_loop
    "\\p{L}", "\\p{N}", "\\p{sc=Han}", "[à-ÿ]", // properties / non-ASCII classes
    ".",                                             // dot
  };

  const std::vector<std::string> k_quants {"", "+", "*", "?", "{2,}", "{3}", "++", "*+"};
  const std::vector<std::string> k_wrap {"%s", "(%s)", "(?:%s)", "(?>%s)"};
  const std::vector<std::string> k_anchor_pre {"", "^", "\\b", "\\A"};
  const std::vector<std::string> k_anchor_post {"", "$", "\\b"};
  const std::vector<std::string> k_look {"", "(?=[a-z])", "(?![0-9])", "(?<=x)"};

  std::string wrap(const std::string& form, const std::string& inner)
  {
    const std::size_t at {form.find("%s")};
    if (at == std::string::npos) {
      return inner;
    }
    return form.substr(0, at) + inner + form.substr(at + 2);
  }

  //! Canonical shapes, one or more per route family. Free composition alone does NOT reach most
  //! routes -- the first run of this probe put 3354 of 3407 compositions on `general_full`, because
  //! every fast route wants a WHOLE-PATTERN shape and almost any added fragment breaks it. Seeding
  //! the shapes and mutating around them is what turns this from a toy into an instrument; it is
  //! also the neighbourhood search that found this engine's sharpest boundaries by hand.
  const std::vector<std::string> k_seeds {
    "[a-z]+", "\\w+", "\\p{L}+", "\\d",                    // class / cp-class / bare cp class
    "abc", "\"abc\"", "\\w+@\\w+", "\\d{4}-\\d{2}-\\d{2}",   // literal, delimited, inner-literal, fixed shape
    "[a-z]{3}[0-9]{2}", "(\\w+)@(\\w+)", "(\\w+)-(\\d+)",        // fixed-shape pair, capturing (one-pass)
    "cat|dog|fish", "[a-z]+(?=[0-9])", "a++", "[a-z]++",     // alternation, trailing LA, possessive
    "\\p{L}++", "\"[a-z]++\"", "^[a-z]+$", "(?i)[a-z]+",       // possessive cp / delimited / anchored / folded
  };

  //! One edit on a seed: the mutation that made every boundary in this engine visible when done by
  //! hand (`\p{L}+` against `\p{L}+a`, eleven branches against twelve).
  std::string mutate(const std::string& seed, std::mt19937& rng)
  {
    const auto pick = [&rng](const std::vector<std::string>& v) -> const std::string& {
                        return v[std::uniform_int_distribution<std::size_t>(0, v.size() - 1)(rng)];
                      };
    switch (std::uniform_int_distribution<int>(0, 4)(rng)) {
      case 0:  return seed + "a";                 // one trailing literal byte
      case 1:  return "a" + seed;                 // one leading literal byte
      case 2:  return pick(k_anchor_pre) + seed;  // an anchor
      case 3:  return seed + pick(k_anchor_post);
      default: return "(" + seed + ")";           // a capture around it
    }
  }

  //! One composed pattern. Deliberately not uniformly random: alternations and concatenations are
  //! drawn often enough to reach the multi-branch and fixed-shape routes, which a flat draw over
  //! atoms would starve.
  std::string compose(std::mt19937& rng)
  {
    const auto pick = [&rng](const std::vector<std::string>& v) -> const std::string& {
                        return v[std::uniform_int_distribution<std::size_t>(0, v.size() - 1)(rng)];
                      };
    // Half the draw goes to seeds and their one-edit neighbours; free composition keeps the rest,
    // since it is what finds shapes nobody thought to seed.
    const int bucket {std::uniform_int_distribution<int>(0, 9)(rng)};
    if (bucket <= 2) {
      return pick(k_seeds);
    }
    if (bucket <= 4) {
      return mutate(pick(k_seeds), rng);
    }
    const int shape {std::uniform_int_distribution<int>(0, 9)(rng)};
    std::string body;
    if (shape <= 4) { // concatenation of 1..3 quantified atoms
      const int n {std::uniform_int_distribution<int>(1, 3)(rng)};
      for (int i = 0; i < n; ++i) {
        body += wrap(pick(k_wrap), pick(k_atoms)) + pick(k_quants);
      }
    }
    else if (shape <= 7) { // alternation of 2..24 literal branches (the AC threshold lives here)
      const int n {std::uniform_int_distribution<int>(2, 24)(rng)};
      for (int i = 0; i < n; ++i) {
        if (i != 0) {
          body += '|';
        }
        body += "w" + std::to_string(i) + static_cast<char>('a' + (i % 26));
      }
    }
    else { // literal-delimited: the shape the inner-literal and possessive-delimited routes want
      body = "\"" + wrap(pick(k_wrap), pick(k_atoms)) + pick(k_quants) + "\"";
    }
    return pick(k_anchor_pre) + body + pick(k_look) + pick(k_anchor_post);
  }

  //! Subjects chosen so a route that needs a match can find one, and one that needs a miss can
  //! miss: a route only reached on success is invisible against a corpus that never matches.
  const std::vector<std::string> k_subjects {
    "the quick brown fox abc xyzzy 12345 a,b,c",
    "Ünïcödé tëxt with àccénts and 日本語 too",
    "\"quoted abc\" and w0a w1b w2c trailing",
    "",
    "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
  };


} // namespace bench_gen

#endif // REAL_BENCH_PATTERN_GEN_HPP
