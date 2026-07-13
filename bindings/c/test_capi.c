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
  real_free(sre);

  printf("capi-test: OK\n");
  return 0;
}
