#define _GNU_SOURCE
// clang-format off
#include <sys/socket.h>
#include "fs_utils.h"
#include "protocol.h"
#include "state_machine.h"
#include <stdio.h>
#include <stdlib.h>
// clang-format on

static void report_cb(const char *json, void *ud) {
  (void)ud;
  if (json)
    printf("%s\n", json);
}

int main(void) {
  char *json = fs_read("sample_recipe.json");
  if (!json) {
    perror("read recipe");
    return 1;
  }

  sm_instr *recipe = proto_parse_recipe(json);
  free(json);
  if (!recipe) {
    fprintf(stderr, "failed to parse recipe\n");
    return 1;
  }

  sm_ctx *ctx = sm_thread_start();
  if (!ctx) {
    fprintf(stderr, "failed to start state machine thread\n");
    return 1;
  }

  sm_set_report_cb(ctx, report_cb, NULL);
  if (!sm_submit(ctx, recipe)) {
    fprintf(stderr, "failed to submit job\n");
    sm_thread_stop(ctx);
    return 1;
  }
  int ret = 0;
  sm_wait(ctx, &ret);
  sm_thread_stop(ctx);

  printf("return %d\n", ret);
  return 0;
}
