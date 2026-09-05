/* Smoke test for the C ABI: compile, iterate matches with group spans, and the error path. */
#include "real_capi.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  char err[256] = {0};
  int code = REAL_ERR_NONE;
  const char* pat = "(\\w+)@(\\w+)";
  real_regex* re = real_compile(pat, strlen(pat), 0, err, sizeof err, &code);
  assert(re != NULL);
  assert(real_group_count(re) == 3); /* group 0 + two groups */

  const char* text = "a@b and cd@ef";
  real_iter* it = real_find_iter(re, text, strlen(text));
  assert(it != NULL);
  size_t spans[6];

  assert(real_iter_next(it, spans) == 1);
  assert(spans[0] == 0 && spans[1] == 3 && spans[2] == 0 && spans[3] == 1 && spans[4] == 2 && spans[5] == 3);
  assert(real_iter_next(it, spans) == 1);
  assert(spans[0] == 8 && spans[1] == 13 && spans[2] == 8 && spans[3] == 10 && spans[4] == 11 && spans[5] == 13);
  assert(real_iter_next(it, spans) == 0); /* end */

  real_iter_free(it);
  real_free(re);

  real_regex* bad = real_compile("(", 1, 0, err, sizeof err, &code);
  assert(bad == NULL);
  assert(strlen(err) > 0);            /* a message was reported */
  assert(code == REAL_ERR_SYNTAX);    /* an unbalanced paren is a syntax error */

  /* the structured code classifies unsupported constructs (well-formed but beyond the linear engine — a named
     backreference here; \p{...} property classes are now supported, so they are no longer an example) */
  real_regex* unsup = real_compile("(?P=g)", 6, 0, err, sizeof err, &code);
  assert(unsup == NULL);
  assert(code == REAL_ERR_UNSUPPORTED);

  /* Every construct the divergences page excludes BY DESIGN must reach a C caller as
     REAL_ERR_UNSUPPORTED, and an extension that is merely unknown as REAL_ERR_SYNTAX. This is the
     only thing a C consumer can branch on: it has no exception type and no message contract. */
  {
    static const char* const excluded[] = {"(a)(?(1)b|c)", "a(?C1)b", "a(?R)b", "(a)(?1)",
                                           "(a)(?-1)",     "(?P<n>a)(?&n)", "(?P<n>a)(?P>n)",
                                           "(?=(?=a))",    "(?<=a*)b",
                                           /* rejected "not supported YET", same classification:
                                              well formed, beyond this engine. A C caller has no
                                              other channel, so it must not read "malformed". */
                                           "(?:ab)*+",     "(?:ab)++", "(?>ab|a)", "(?=a{1,3}+)b"};
    static const char* const malformed[] = {"(?Z)", "(unclosed", "a)", "a{3,1}"};
    size_t i;
    for (i = 0; i < sizeof excluded / sizeof excluded[0]; ++i) {
      real_regex* r = real_compile(excluded[i], strlen(excluded[i]), 0, err, sizeof err, &code);
      assert(r == NULL);
      assert(code == REAL_ERR_UNSUPPORTED);
      assert(strstr(err, "unknown extension") == NULL); /* named, not reported as a typo */
    }
    assert(i == 13); /* denominator: a row deleted rather than fixed must fail here */
    for (i = 0; i < sizeof malformed / sizeof malformed[0]; ++i) {
      real_regex* r = real_compile(malformed[i], strlen(malformed[i]), 0, err, sizeof err, &code);
      assert(r == NULL);
      assert(code == REAL_ERR_SYNTAX);
    }
    assert(i == 4);
  }

  /* group names + find-at */
  const char* npat = "(?P<user>\\w+)@(?P<host>\\w+)";
  real_regex* named = real_compile(npat, strlen(npat), 0, err, sizeof err, &code);
  assert(named != NULL);
  char name[64];
  assert(real_group_name(named, 0, name, sizeof name) == 0 && name[0] == '\0'); /* group 0 unnamed */
  assert(real_group_name(named, 1, name, sizeof name) == 4 && strcmp(name, "user") == 0);
  assert(real_group_name(named, 2, name, sizeof name) == 4 && strcmp(name, "host") == 0);

  const char* nt = "a@b cd@ef";
  real_iter* at = real_find_iter_at(named, nt, strlen(nt), 4); /* search from offset 4: skips a@b */
  size_t ns[6];
  assert(real_iter_next(at, ns) == 1 && ns[0] == 4 && ns[1] == 9); /* cd@ef */
  assert(real_iter_next(at, ns) == 0);
  real_iter_free(at);

  real_free(named);

  /* real_find_iter_between: bounded on both sides, unlike find_iter_at's open-ended end */
  const char* wpat = "\\w+";
  real_regex* wre = real_compile(wpat, strlen(wpat), 0, err, sizeof err, &code);
  assert(wre != NULL);
  const char* wt = "aa bb aaa cc aa"; /* "aa"[0,2) "bb"[3,5) "aaa"[6,9) "cc"[10,12) "aa"[13,15) */
  real_iter* bt = real_find_iter_between(wre, wt, strlen(wt), 3, 9); /* region: "aa bb aaa" */
  size_t bs[2];
  assert(real_iter_next(bt, bs) == 1 && bs[0] == 3 && bs[1] == 5);  /* "bb" */
  assert(real_iter_next(bt, bs) == 1 && bs[0] == 6 && bs[1] == 9);  /* "aaa" */
  assert(real_iter_next(bt, bs) == 0);                              /* "cc"/"aa" outside [3,9) */
  real_iter_free(bt);
  real_free(wre);

  /* real_match: search/match/fullmatch as one function, region-aware */
  const char* mpat = "a(b)c";
  real_regex* mre = real_compile(mpat, strlen(mpat), 0, err, sizeof err, &code);
  assert(mre != NULL);
  size_t ms[4];

  assert(real_match(mre, "xxabcyy", 7, 0, 7, REAL_MODE_SEARCH, ms) == 1);
  assert(ms[0] == 2 && ms[1] == 5 && ms[2] == 3 && ms[3] == 4); /* "abc", group1 "b" */

  assert(real_match(mre, "abcxyz", 6, 0, 6, REAL_MODE_MATCH, ms) == 1);
  assert(ms[0] == 0 && ms[1] == 3);
  assert(real_match(mre, "xabc", 4, 0, 4, REAL_MODE_MATCH, ms) == 0); /* not anchored at 0 */

  assert(real_match(mre, "abc", 3, 0, 3, REAL_MODE_FULLMATCH, ms) == 1);
  assert(real_match(mre, "abcx", 4, 0, 4, REAL_MODE_FULLMATCH, ms) == 0); /* trailing byte */

  /* region params: fullmatch over the SUBREGION [1,4) of "xabcx" ("abc") */
  assert(real_match(mre, "xabcx", 5, 1, 4, REAL_MODE_FULLMATCH, ms) == 1);
  assert(ms[0] == 1 && ms[1] == 4);

  assert(real_match(NULL, "x", 1, 0, 1, REAL_MODE_SEARCH, ms) == -1); /* null re */
  real_free(mre);

  /* real_sub: two-call sizing/fill convention, group swap, count limit, octal escape, error path */
  real_regex* sre = real_compile(pat, strlen(pat), 0, err, sizeof err, &code); /* "(\\w+)@(\\w+)" */
  assert(sre != NULL);
  const char* stext = "a@b and cd@ef";
  const char* repl = "\\2@\\1"; /* swap user/host */
  size_t n_subs = 0;

  size_t need = real_sub(sre, stext, strlen(stext), repl, strlen(repl), 0, NULL, 0, &n_subs, err, sizeof err);
  assert(need == strlen("b@a and ef@cd"));
  assert(n_subs == 2);
  char subbuf[64];
  size_t need2 = real_sub(sre, stext, strlen(stext), repl, strlen(repl), 0, subbuf, sizeof subbuf, &n_subs,
                          err, sizeof err);
  assert(need2 == need);
  assert(memcmp(subbuf, "b@a and ef@cd", need) == 0);

  /* count=1: only the first match replaced */
  size_t need1 = real_sub(sre, stext, strlen(stext), repl, strlen(repl), 1, subbuf, sizeof subbuf, &n_subs,
                          err, sizeof err);
  assert(n_subs == 1);
  assert(memcmp(subbuf, "b@a and cd@ef", need1) == 0);

  /* octal escape: one raw byte, never re-encoded (byte-oriented ABI) */
  real_regex* xre = real_compile("x", 1, 0, err, sizeof err, &code);
  assert(xre != NULL);
  size_t oct_needed = real_sub(xre, "x", 1, "\\101", 4, 0, subbuf, sizeof subbuf, &n_subs, err, sizeof err);
  assert(oct_needed == 1 && subbuf[0] == 'A');
  real_free(xre);

  /* error path: an out-of-range group reference fails cleanly, errbuf filled, no crash */
  err[0] = '\0';
  size_t bad_sub = real_sub(sre, stext, strlen(stext), "\\9", 2, 0, NULL, 0, NULL, err, sizeof err);
  assert(bad_sub == (size_t) -1);
  assert(strlen(err) > 0);

  /* real_expand: real_sub's inner half, one match at a time, so a caller whose flavor enumerates
     matches differently keeps ONE template grammar instead of writing a second one. Same two-call
     convention; the spans are whatever real_match just filled. */
  size_t espans[6];
  assert(real_group_count(sre) == 3);
  assert(real_match(sre, stext, strlen(stext), 0, strlen(stext), REAL_MODE_SEARCH, espans) == 1);
  size_t exp_need = real_expand(sre, stext, strlen(stext), espans, 6, repl, strlen(repl),
                                NULL, 0, err, sizeof err);
  assert(exp_need == strlen("b@a"));
  char expbuf[32];
  size_t exp_need2 = real_expand(sre, stext, strlen(stext), espans, 6, repl, strlen(repl),
                                 expbuf, sizeof expbuf, err, sizeof err);
  assert(exp_need2 == exp_need);
  assert(memcmp(expbuf, "b@a", exp_need) == 0);

  /* Expanding the SECOND match must give that match's groups, not the first's -- the whole point of
     supplying spans rather than letting the callee find them. */
  assert(real_match(sre, stext, strlen(stext), 5, strlen(stext), REAL_MODE_SEARCH, espans) == 1);
  size_t exp2 = real_expand(sre, stext, strlen(stext), espans, 6, repl, strlen(repl),
                            expbuf, sizeof expbuf, err, sizeof err);
  assert(memcmp(expbuf, "ef@cd", exp2) == 0);

  /* A spans buffer too short for the group the template names is the caller's error, distinct from
     a group the PATTERN lacks: both must fail, neither may read past the buffer. */
  err[0] = '\0';
  assert(real_expand(sre, stext, strlen(stext), espans, 2, repl, strlen(repl),
                     NULL, 0, err, sizeof err) == (size_t) -1);
  assert(strlen(err) > 0);
  err[0] = '\0';
  assert(real_expand(sre, stext, strlen(stext), espans, 6, "\\9", 2,
                     NULL, 0, err, sizeof err) == (size_t) -1);
  assert(strlen(err) > 0);

  /* An inverted or out-of-subject span is refused rather than turned into a length underflow. */
  err[0] = '\0';
  size_t bad_spans[6] = {4, 1, 4, 1, 4, 1};
  assert(real_expand(sre, stext, strlen(stext), bad_spans, 6, repl, strlen(repl),
                     NULL, 0, err, sizeof err) == (size_t) -1);
  assert(strlen(err) > 0);
  err[0] = '\0';
  assert(real_expand(NULL, stext, strlen(stext), espans, 6, repl, strlen(repl),
                     NULL, 0, err, sizeof err) == (size_t) -1);

  /* An unmatched OPTIONAL group contributes nothing here too, exactly as it does in real_sub. */
  real_regex* ore = real_compile("(a)(b)?", 7, 0, err, sizeof err, &code);
  assert(ore != NULL);
  size_t ospans[6];
  assert(real_match(ore, "a", 1, 0, 1, REAL_MODE_SEARCH, ospans) == 1);
  size_t opt_need = real_expand(ore, "a", 1, ospans, 6, "[\\1\\2]", 6,
                                expbuf, sizeof expbuf, err, sizeof err);
  assert(opt_need == 3);
  assert(memcmp(expbuf, "[a]", 3) == 0);
  real_free(ore);

  /* (NULL, 0) empty-subject convention: valid everywhere a (text, len) or (repl, repl_len) pair
     appears -- the natural Go nil-slice representation. Only NULL with a NONZERO length errors. */
  const char* epat = "";
  real_regex* ere = real_compile(epat, 0, 0, err, sizeof err, &code); /* empty pattern */
  assert(ere != NULL);

  assert(real_match(ere, NULL, 0, 0, 0, REAL_MODE_FULLMATCH, ms) == 1); /* "" fullmatches "" */
  /* an empty pattern matches ONCE at position 0, even in an empty subject (re's own semantics:
     re.findall("", "") == ['']) -- 1, not 0, and NOT -1 (the fix under test: NULL text no longer
     unconditionally errors). */
  assert(real_count_matches(ere, NULL, 0) == 1);
  assert(real_match(sre, NULL, 5, 0, 5, REAL_MODE_SEARCH, ms) == -1); /* NULL + nonzero len: still an error */

  /* The header states the (NULL, nonzero) rule ONCE for the whole ABI, so every door owes the same
     sentinel. Eight already returned it; five dereferenced instead, and a segfault is the one
     failure mode this boundary promises never to have ("never a crash or a propagated throw").
     Asserted door by door, because "the ABI guards NULL" was true of a majority and false of a
     third -- which is how a contract with two doors survives. */
  assert(real_find_iter(sre, NULL, 4) == NULL);
  assert(real_find_iter_at(sre, NULL, 4, 0) == NULL);
  assert(real_find_iter_between(sre, NULL, 4, 0, 4) == NULL);
  assert(real_compile(NULL, 5, 0, err, sizeof err, &code) == NULL);
  assert(strlen(err) > 0); /* and it says why, rather than failing silently */
  {
    const char*  null_member[1] = {NULL};
    const size_t null_member_len[1] = {5};
    assert(real_set_compile(null_member, null_member_len, 1, 0, err, sizeof err, &code) == NULL);
  }

  /* The other half of the same rule, which must NOT become an error: (NULL, 0) is a valid empty
     pattern and a valid empty subject everywhere, and Go's nil slice depends on it. */
  {
    real_regex* nre = real_compile(NULL, 0, 0, err, sizeof err, &code);
    assert(nre != NULL);
    assert(real_find_iter(nre, NULL, 0) != NULL);
    real_iter* nit = real_find_iter(nre, NULL, 0);
    real_iter_free(nit);
    real_free(nre);
  }

  size_t sub_need = real_sub(ere, NULL, 0, NULL, 0, 0, NULL, 0, &n_subs, err, sizeof err);
  assert(sub_need == 0 && n_subs == 1); /* the one empty match gets "substituted" with nothing */

  real_free(ere);
  real_free(sre);

  const char* set_pats[2] = {"a", "b"};
  const size_t set_lens[2] = {1, 1};
  real_regex_set* set = real_set_compile(set_pats, set_lens, 2, 0, err, sizeof err, &code);
  assert(set != NULL);
  assert(real_set_is_match(set, NULL, 0) == 0); /* no match in an empty subject: 0, not -1 */
  uint8_t hits[2];
  assert(real_set_matches(set, NULL, 0, hits) == 0);
  assert(hits[0] == 0 && hits[1] == 0);
  real_set_free(set);

  /* null real_regex* handle contracts (use-after-free / closed-handle safety for bindings) */
  assert(real_group_count(NULL) == 0);
  char null_name[8];
  null_name[0] = 'x';
  assert(real_group_name(NULL, 1, null_name, sizeof null_name) == 0 && null_name[0] == '\0');
  assert(real_find_iter(NULL, "ab", 2) == NULL);
  assert(real_find_iter_at(NULL, "ab", 2, 0) == NULL);
  assert(real_find_iter_between(NULL, "ab", 2, 0, 2) == NULL);
  real_free(NULL); /* intentional no-op */

  printf("capi-test: OK\n");
  return 0;
}
