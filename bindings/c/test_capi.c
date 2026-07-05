/* Smoke test for the C ABI: compile, iterate matches with group spans, and the error path. */
#include "real_capi.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  char err[256] = {0};
  const char* pat = "(\\w+)@(\\w+)";
  real_regex* re = real_compile(pat, strlen(pat), 0, err, sizeof err);
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

  real_regex* bad = real_compile("(", 1, 0, err, sizeof err);
  assert(bad == NULL);
  assert(strlen(err) > 0); /* a message was reported */

  /* group names + find-at */
  const char* npat = "(?P<user>\\w+)@(?P<host>\\w+)";
  real_regex* named = real_compile(npat, strlen(npat), 0, err, sizeof err);
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

  printf("capi-test: OK\n");
  return 0;
}
