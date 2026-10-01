//! A non-contiguous range -- a deque, a list, a reverse iterator -- is searched on REAL over one contiguous copy, with
//! the positions mapped back to the caller's own iterators: the same answers as on a string, linear time, and
//! O(n) memory for the copy. These pin the answers, the iterators handed back, the cost by counting the caller's
//! iterator steps, and which types compile.
#include <cstdint>
#include <deque>
#include <iterator>
#include <list>
#include <memory>
#include <regex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/compat/std/regex.hpp"

namespace rc = real::compat;

namespace {

  using DqIt = std::deque<char>::const_iterator;
  using LsIt = std::list<char>::const_iterator;
  using StIt = std::string::const_iterator;
  using RvIt = std::string::const_reverse_iterator;

  template <typename It>
  concept searches = requires(It a, rc::match_results<It>& m, const rc::regex& re) {
    rc::regex_search(a, a, m, re);
    rc::regex_match(a, a, m, re);
    rc::regex_search(a, a, re);
    rc::regex_match(a, a, re);
    rc::regex_iterator<It>(a, a, re);
    rc::regex_token_iterator<It>(a, a, re, -1);
  };

  template <typename It>
  concept has_view = requires(const rc::sub_match<It>& s) {
    s.view();
  };

  // Every operation takes any bidirectional iterator; view() only exists over contiguous storage, where it cannot
  // dangle; a contiguous iterator carries nothing for the copy.
  static_assert(searches<DqIt> && searches<LsIt> && searches<RvIt> && searches<StIt> && searches<const char*>);
  static_assert(!has_view<LsIt> && !has_view<DqIt> && !has_view<RvIt> && has_view<StIt> && has_view<const char*>);
  static_assert(std::is_same_v<decltype(std::declval<const rc::sub_match<LsIt>&>().str()), std::string>);
  static_assert(std::is_same_v<rc::detail::subject_for<StIt>, rc::detail::in_place_subject>);
  static_assert(sizeof(rc::regex_iterator<StIt>) < sizeof(rc::regex_iterator<LsIt>));

  //! (position, length) of every group of \p m, -1 for an unmatched one.
  template <typename It>
  std::vector<std::pair<long long, long long>> groups_of(const rc::match_results<It>& m)
  {
    std::vector<std::pair<long long, long long>> out;
    for (std::size_t g {0}; g < m.size(); ++g) {
      out.emplace_back(m[g].matched ? static_cast<long long>(m.position(g)) : -1LL,
                       m[g].matched ? static_cast<long long>(m.length(g)) : -1LL);
    }
    return out;
  }

  //! Every match of \p re over [first, last) by the iterator, as (position, length) of each group.
  template <typename It>
  std::vector<std::vector<std::pair<long long, long long>>> walk(It               first,
                                                                 It               last,
                                                                 const rc::regex& re)
  {
    std::vector<std::vector<std::pair<long long, long long>>> out;
    for (rc::regex_iterator<It> it {first, last, re}, end; it != end; it++) { // it++: a copy every step
      out.push_back(groups_of(*it));
    }
    return out;
  }

  long long steps {0};

  //! A list iterator that counts every step, so the cost is checked by the caller's own walk, not by a clock.
  class counted
  {
  public:

    using base              = std::list<char>::const_iterator;
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type        = char;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const char*;
    using reference         = const char&;

    counted() = default;

    explicit counted(base it) : it_ {it}
    {}

    reference operator*() const
    {
      return *it_;
    }

    pointer operator->() const
    {
      return &*it_;
    }

    counted& operator++()
    {
      ++steps;
      ++it_;
      return *this;
    }

    counted operator++(int)
    {
      const counted old {*this};
      ++*this;
      return old;
    }

    counted& operator--()
    {
      ++steps;
      --it_;
      return *this;
    }

    counted operator--(int)
    {
      const counted old {*this};
      --*this;
      return old;
    }

    bool operator==(const counted& other) const
    {
      return it_ == other.it_;
    }

  private:

    base it_;
  };

  static_assert(std::bidirectional_iterator<counted> && !std::random_access_iterator<counted>);
} // namespace

// Search and match, with and without results, answer on a deque, a list and a reverse range as on a string -- the
// deque crossing one of its blocks -- and the iterators handed back are the caller's.
TEST(compat_non_contiguous_answers_as_on_a_string)
{
  std::string subject(4093, 'x');
  subject += " foo=bar baz=42 qux bar=foo ";
  const std::deque<char> deque(subject.begin(), subject.end());
  const std::list<char>  list(subject.begin(), subject.end());
  const char* const      patterns[] {R"((\w+)=(\w+))", R"(\bbaz\b)", R"((?<=x )foo)", R"(q(u)?x)", "x*"};
  for (const char* const p : patterns) {
    const rc::regex         re    {p};
    rc::smatch              on_string;
    const bool              found {rc::regex_search(subject, on_string, re)};
    rc::match_results<DqIt> on_deque;
    rc::match_results<LsIt> on_list;
    EXPECT_EQ(rc::regex_search(deque.begin(), deque.end(), on_deque, re), found);
    EXPECT_EQ(rc::regex_search(list.begin(), list.end(), on_list, re), found);
    EXPECT_EQ(rc::regex_search(deque.begin(), deque.end(), re), found);
    EXPECT_EQ(rc::regex_search(list.begin(), list.end(), re), found);
    if (found) {
      EXPECT(groups_of(on_deque) == groups_of(on_string));
      EXPECT(groups_of(on_list) == groups_of(on_string));
      EXPECT_EQ(on_list.str(0), on_string.str(0));
      const auto at {std::next(list.begin(), static_cast<long>(on_string.position(0)))};
      EXPECT(&*on_list[0].first == &*at); // the caller's element, not one of the copy
    }
    EXPECT(walk(deque.begin(), deque.end(), re) == walk(subject.cbegin(), subject.cend(), re));
    EXPECT(walk(list.begin(), list.end(), re) == walk(subject.cbegin(), subject.cend(), re));
  }
  const rc::regex whole {R"(x+ foo=bar.*)"};
  EXPECT(rc::regex_match(list.begin(), list.end(), whole));
  rc::match_results<LsIt> whole_list;
  EXPECT(rc::regex_match(list.begin(), list.end(), whole_list, whole));
  EXPECT_EQ(whole_list.length(0), static_cast<long long>(subject.size()));
  const std::string       reversed {"rab=oof"};
  rc::match_results<RvIt> backwards;
  EXPECT(rc::regex_search(reversed.crbegin(), reversed.crend(), backwards, rc::regex {R"((\w+)=(\w+))"}));
  EXPECT_EQ(backwards.str(1), "foo");
}

// An iterator copied, moved to the heap or post-incremented keeps walking the one copy it made; token iterators
// and the range form of replace answer as on a string.
TEST(compat_non_contiguous_iterators_tokens_and_replace)
{
  const std::string              subject {"a=1, b=22, c=333"};
  const std::list<char>          list(subject.begin(), subject.end());
  const rc::regex                re      {R"((\w)=(\d+))"};
  rc::regex_iterator<LsIt>       it      {list.begin(), list.end(), re};
  const rc::regex_iterator<LsIt> copy    {it};
  auto                           moved   {std::make_unique<rc::regex_iterator<LsIt>>(std::move(it))};
  ++*moved;
  EXPECT_EQ((**moved).str(1), "b");
  EXPECT_EQ((*copy).str(1), "a");
  std::vector<std::string> tokens;
  for (rc::regex_token_iterator<LsIt> t {list.begin(), list.end(), re, {-1, 1, 2}}, end; t != end; ++t) {
    tokens.push_back(t->str());
  }
  const std::vector<std::string> want {"", "a", "1", ", ", "b", "22", ", ", "c", "333"};
  EXPECT(tokens == want);
  std::string replaced;
  rc::regex_replace(std::back_inserter(replaced), list.begin(), list.end(), re, std::string {"$2$1"});
  EXPECT_EQ(replaced, rc::regex_replace(subject, re, std::string {"$2$1"}));
}

// A pattern that backtracks exponentially stays linear on a non-contiguous range, where std's would not: the copy
// runs on REAL.
TEST(compat_non_contiguous_runs_in_linear_time)
{
  std::string            subject(20000, 'a');
  const std::deque<char> deque(subject.begin(), subject.end());
  const rc::regex        re {"(a+)+b"};
  EXPECT(re.uses_real());
  EXPECT(!rc::regex_search(deque.begin(), deque.end(), re));
}

// The cost, by the caller's own steps: the copy is 2n on a list (measured, then copied) and mapping the positions
// back at most n more, for a search and for a whole iteration alike -- never a walk from the start per group or
// per match.
TEST(compat_non_contiguous_cost_is_linear_in_steps)
{
  constexpr long long n {20000};
  {
    std::string nested(static_cast<std::size_t>(n), 'a');
    nested += 'b';
    const std::list<char>      list(nested.begin(), nested.end());
    rc::match_results<counted> m;
    steps = 0;
    EXPECT(rc::regex_search(counted {list.begin()}, counted {list.end()}, m, rc::regex {"((a+)b)"}));
    EXPECT(steps <= (3 * n) + 16);
    EXPECT_EQ(m.str(2).size(), static_cast<std::size_t>(n));
  }
  std::string pairs;
  for (long long i {0}; i < n / 2; ++i) {
    pairs += i % 7 == 0 ? "a-" : "ab";
  }
  const std::list<char> list(pairs.begin(), pairs.end());
  const rc::regex       pair_re {"(a)(b)?"};
  steps = 0;
  long long count               {0};
  for (rc::regex_iterator<counted> it {counted {list.begin()}, counted {list.end()}, pair_re}, end; it != end; it++) {
    ++count;
  }
  EXPECT_EQ(count, n / 2);
  EXPECT(steps <= (3 * n) + 16);
  const rc::regex       star {"a*"};
  const std::string     dashes(static_cast<std::size_t>(n), '-');
  const std::list<char> empties(dashes.begin(), dashes.end());
  steps = 0;
  count = 0;
  for (rc::regex_iterator<counted> it {counted {empties.begin()}, counted {empties.end()}, star}, end;
       it != end; ++it) {
    ++count;
  }
  EXPECT_EQ(count, n + 1);
  EXPECT(steps <= (3 * n) + 16);
}

// match_prev_avail on a sub-range of a deque or a list reads the context from the caller's sequence (std's route,
// on the caller's iterators), as std does.
TEST(compat_non_contiguous_prev_avail_reads_the_callers_context)
{
  const std::string                                   subject {"ab ab"};
  const std::list<char>                               list(subject.begin(), subject.end());
  const std::deque<char>                              deque(subject.begin(), subject.end());
  const rc::regex                                     re    {R"(\bb)"};
  const std::regex                                    ref   {R"(\bb)"};
  const auto                                          flags {rc::regex_constants::match_prev_avail};
  std::match_results<std::list<char>::const_iterator> want_list;
  const bool                                          want  {std::regex_search(std::next(list.begin()), list.end(), want_list, ref, std::regex_constants::match_prev_avail)};
  EXPECT_EQ(rc::regex_search(std::next(list.begin()), list.end(), re, flags), want);
  EXPECT_EQ(rc::regex_search(std::next(deque.begin()), deque.end(), re, flags), want);
  EXPECT(!want); // 'a' before the 'b': no boundary there, and the second 'b' follows an 'a' too
}
