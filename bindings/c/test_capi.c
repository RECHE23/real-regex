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

  printf("capi-test: OK\n");
  return 0;
}
