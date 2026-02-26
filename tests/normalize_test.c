/* Copyright (c) 2025 Contributors */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hex.h"

static void expect_normalize(const char *input, size_t input_len, const char *expected, size_t expected_len) {
  char buf[128];
  assert(input_len < sizeof(buf));
  memcpy(buf, input, input_len);
  size_t out_len = normalize_line(buf, input_len);
  assert(out_len == expected_len);
  assert(memcmp(buf, expected, expected_len) == 0);
  assert(buf[out_len] == '\0');
}

static void run_hexln_case(const char *printf_arg, const char *expected) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "printf '%s' | ./hexln", printf_arg);
  FILE *pipe = popen(cmd, "r");
  assert(pipe != NULL);
  char out[256];
  char *line = fgets(out, sizeof(out), pipe);
  int status = pclose(pipe);
  assert(status == 0);
  assert(line != NULL);
  assert(strcmp(out, expected) == 0);
}

int main(void) {
  expect_normalize("word\n", 5, "word", 4);
  expect_normalize("word\r\n", 6, "word", 4);
  expect_normalize("word\r", 5, "word", 4);
  expect_normalize("word", 4, "word", 4);

  run_hexln_case("abc", "616263\n");
  run_hexln_case("abc\\r\\n", "616263\n");
  run_hexln_case("abc\\r", "616263\n");

  printf("OK\n");
  return 0;
}

