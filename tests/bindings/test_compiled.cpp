//! The compiled-library façade (bindings/c/real_compiled.hpp) against the header-only engine it wraps: the
//! same spans for every mode and region, the same refusals, and results that own their spans.
#include <sciforge/test/framework.hpp>

#include <real_compiled.hpp>

#include <real/real.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

  //! The spans of a header-only match, npos-padded like the façade's.
  template <typename Match>
  std::vector<std::size_t> spans_of(const Match& m)
  {
    std::vector<std::size_t> out;
    if (m) {
      for (std::size_t g {0}; g < m.size(); ++g) {
        out.push_back(m.start(g));
        out.push_back(m.end(g));
      }
    }
    return out;
  }

  std::vector<std::size_t> spans_of(const real::compiled::match_result& m)
  {
    std::vector<std::size_t> out;
    for (std::size_t g {0}; g < m.size(); ++g) {
      out.push_back(m.start(g));
      out.push_back(m.end(g));
    }
    return out;
  }
} // namespace

// Every mode, on whole subjects and on regions, over patterns that reach the engine's routes -- literal,
// class runs, captures, alternation, anchors, word boundaries, Unicode classes, lazy and possessive loops.
TEST(compiled_agrees_with_the_engine_on_every_mode_and_region)
{
  const std::string_view patterns[] {R"((\w+)@(\w+))", "dog|fox", "[a-z]+$", "^(a|ab)(c|bcd)(d*)$", R"(\bfoo\b)",
                                     R"((?P<y>\d{4})-(?P<m>\d\d))", "é+", "a*?b", "x*+y", "(a)|(b)", R"(\s*$)", ""};
  const std::string_view subjects[] {"", "abc@def and x@y", "the dog and the fox", "abcd", "a foo b", "2026-09 or 1999-12",
                                     "ééé", "aaab", "xxy", "b", "line \n", "\xC3"};
  int                    compared   {0};
  for (const std::string_view pattern : patterns) {
    const real::regex           engine {pattern};
    const real::compiled::regex facade {pattern};
    EXPECT_EQ(facade.group_count(), engine.group_count());
    for (const std::string_view text : subjects) {
      for (std::size_t start {0}; start <= text.size(); ++start) {
        EXPECT(spans_of(facade.search(text, start)) == spans_of(engine.search(text, start)));
        EXPECT(spans_of(facade.match(text, start)) == spans_of(engine.match(text, start)));
        EXPECT(spans_of(facade.fullmatch(text, start)) == spans_of(engine.fullmatch(text, start)));
        const std::size_t end {text.size() - ((text.size() - start) / 2U)};
        EXPECT(spans_of(facade.search(text, start, end)) == spans_of(engine.search(text, start, end)));
        ++compared;
      }
      std::vector<std::vector<std::size_t>> engine_all;
      for (const auto& m : engine.find_iter(text)) {
        engine_all.push_back(spans_of(m));
      }
      std::vector<std::vector<std::size_t>> facade_all;
      for (const auto& m : facade.find_all(text)) {
        facade_all.push_back(spans_of(m));
      }
      EXPECT(facade_all == engine_all);
      EXPECT_EQ(facade.count(text), engine_all.size());
    }
  }
  EXPECT(compared >= 600);
}

// A refusal carries the engine's classification and position.
TEST(compiled_refuses_what_the_engine_refuses)
{
  for (const std::string_view pattern : {"(", "a{2,1}", "(?<=a*)b", "x)", "\\p{Nope}"}) {
    std::optional<real::regex_error> engine_error;
    try {
      static_cast<void>(real::regex {pattern});
    }
    catch (const real::regex_error& e) {
      engine_error = e;
    }
    std::optional<real::compiled::error> facade_error;
    try {
      static_cast<void>(real::compiled::regex {pattern});
    }
    catch (const real::compiled::error& e) {
      facade_error = e;
    }
    EXPECT(engine_error.has_value());
    EXPECT(facade_error.has_value());
    if (engine_error && facade_error) {
      EXPECT_EQ(facade_error->position(), engine_error->position());
      EXPECT_EQ(std::string_view(facade_error->what()), std::string_view(engine_error->cause()));
      EXPECT_EQ(facade_error->code(), engine_error->kind() == real::error_kind::unsupported ? REAL_ERR_UNSUPPORTED
                                                                                           : REAL_ERR_SYNTAX);
    }
  }
}

// A copy shares the one compiled program and keeps it alive past the original; a result owns its spans
// and outlives every handle. Under ASan a result that borrowed from the handle would read freed memory.
TEST(compiled_results_outlive_their_regex)
{
  const std::string                         text {"mail bob@host and amy@site"};
  std::optional<real::compiled::regex>      copy;
  std::vector<real::compiled::match_result> all;
  {
    const real::compiled::regex re {R"((\w+)@(\w+))"};
    copy.emplace(re);
    all = re.find_all(text);
  }
  const real::compiled::match_result first {copy->search(text)}; // the original is gone
  copy.reset();                                                  // and now every handle
  EXPECT(static_cast<bool>(first));
  EXPECT_EQ(first.str(1), std::string_view {"bob"});
  EXPECT_EQ(first.str(2), std::string_view {"host"});
  EXPECT_EQ(all.size(), static_cast<std::size_t>(2));
  EXPECT_EQ(all[1].str(0), std::string_view {"amy@site"});
  EXPECT(!first.matched(3));
  EXPECT_EQ(first.start(3), real::compiled::npos);
  EXPECT(first.str(3).empty());
}

TEST(compiled_names_flags_and_substitution)
{
  const real::compiled::regex re {R"((?P<user>\w+)@(?P<host>\w+))"};
  EXPECT_EQ(re.group_name(1), std::string {"user"});
  EXPECT_EQ(re.group_name(2), std::string {"host"});
  EXPECT(re.group_name(0).empty());
  EXPECT_EQ(re.group_index("host"), static_cast<std::size_t>(2));
  EXPECT_EQ(re.group_index("nope"), real::compiled::npos);
  EXPECT_EQ(re.pattern(), std::string {R"((?P<user>\w+)@(?P<host>\w+))"});
  EXPECT_EQ(re.sub(R"(\g<host>:\1)", "a@b c@d"), std::string {"b:a d:c"});
  EXPECT_EQ(re.sub("X", "a@b c@d", 1), std::string {"X c@d"});

  bool refused {false};
  try {
    static_cast<void>(re.sub(R"(\9)", "a@b"));
  }
  catch (const real::compiled::error& e) {
    refused = e.position() == real::compiled::npos && e.code() == 0;
  }
  EXPECT(refused);

  const real::compiled::regex caseless {"DOG", real::compiled::flags::icase | real::compiled::flags::ascii};
  EXPECT(static_cast<bool>(caseless.search("a dog")));
  const real::compiled::regex plain    {"DOG"};
  EXPECT(!plain.search("a dog"));
  EXPECT(real::compiled::abi_matches());
}
