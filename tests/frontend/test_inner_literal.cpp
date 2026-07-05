//! IL.0a fixtures: the inner-literal extraction is a pure function on the AST. This pins the classification
//! of the duel + flagship patterns (extracted vs declined) and the soundness acid — a pattern with a path
//! that bypasses the literal must NOT extract.
#include <sciforge/test/framework.hpp>

#include <real/frontend/inner_literal.hpp>

#include <cstring>
#include <string_view>

namespace {
  real::detail::inner_literal extract(std::string_view pattern,
                                      real::flags      f = real::flags::none)
  {
    return real::detail::extract_inner_literal(real::detail::parse(pattern, f));
  }

  bool is_lit(const real::detail::inner_literal& il,
              std::string_view                   expected)
  {
    return il.len == expected.size()
           && std::memcmp(il.bytes.data(), expected.data(), il.len) == 0;
  }
}

TEST(inner_literal_extracted)
{
  // The required literal every match must contain — the maximal guaranteed byte run, most-selective wins.
  EXPECT(is_lit(extract(R"((\w+)@(\w+))"), "@"));        // the @ between two word runs
  EXPECT(is_lit(extract(R"(\d{4}-\d{2}-\d{2})"), "-"));  // candidate-first: the - has no fixed offset
  EXPECT(is_lit(extract(R"(key=(\w+))"), "key="));       // a 4-byte run beats its single rare byte
  EXPECT(is_lit(extract(R"((\w+)_(\w+))"), "_"));        // extracted even though the ident arc is separate
  EXPECT(is_lit(extract(R"((foo)bar)"), "foobar"));      // descends the group; the run spans the boundary
}

TEST(inner_literal_declined_soundness)
{
  // The acid: a bypassing path means the literal is NOT required -> no extraction.
  EXPECT(!extract(R"((a)?@b)").found());   // (a)? optional -> conservative decline
  EXPECT(!extract(R"(foo|@bar)").found()); // alternation -> no common required literal
  EXPECT(!extract(R"(x*@?y)").found());    // @? optional -> @ is not guaranteed
}

TEST(inner_literal_declined_routed)
{
  EXPECT(!extract(R"(^foo)").found());       // anchored -> handled without a scan
  EXPECT(!extract(R"((?=foo)bar)").found()); // lookaround -> VM-routed
  EXPECT(!extract(R"(a|b)").found());        // top-level alternation
  EXPECT(!extract(R"(\w+)").found());        // no literal run at all
}

TEST(inner_literal_bytes_mode)
{
  // Literals are bytes, so the extraction works identically in bytes mode.
  EXPECT(is_lit(extract(R"((\w+)@(\w+))", real::flags::bytes), "@"));
}
