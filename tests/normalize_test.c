/* Copyright (c) 2025 Contributors */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

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

/* TC-07: brainflayer handles dictionary with mixed newlines without crashing */
static void test_brainflayer_mixed_newlines(void) {
  char dict_path[] = "/tmp/bf_test_dict_XXXXXX";
  int fd = mkstemp(dict_path);
  assert(fd >= 0);
  const char data[] = "alpha\nbravo\r\ncharlie\rdelta";
  FILE *f = fdopen(fd, "w");
  assert(f != NULL);
  fputs(data, f);
  fclose(f);

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "./brainflayer -t sha256 -B 8 -i %s >/dev/null 2>&1", dict_path);
  int ret = system(cmd);
  unlink(dict_path);
  assert(WIFEXITED(ret) && WEXITSTATUS(ret) == 0);
}

/* TC-09: hex2blf + blfchk round-trip: a hash added to the bloom filter is found */
static void test_bloom_roundtrip(void) {
  const char *hash_line = "000102030405060708090a0b0c0d0e0f10111213\n";

  char hash_path[]  = "/tmp/bf_test_hash_XXXXXX";
  char bloom_path[] = "/tmp/bf_test_bloom_XXXXXX";

  int hfd = mkstemp(hash_path);
  assert(hfd >= 0);
  FILE *hf = fdopen(hfd, "w");
  assert(hf != NULL);
  fputs(hash_line, hf);
  fclose(hf);

  /* hex2blf creates the bloom file; remove the placeholder so it starts fresh */
  int bfd = mkstemp(bloom_path);
  close(bfd);
  unlink(bloom_path);

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "./hex2blf %s %s 2>/dev/null", hash_path, bloom_path);
  int ret = system(cmd);
  assert(WIFEXITED(ret) && WEXITSTATUS(ret) == 0);

  /* feed the same hash through blfchk and verify it appears in output */
  snprintf(cmd, sizeof(cmd),
    "echo '000102030405060708090a0b0c0d0e0f10111213' | ./blfchk %s 2>/dev/null"
    " | grep -qF '000102030405060708090a0b0c0d0e0f10111213'",
    bloom_path);
  ret = system(cmd);

  unlink(hash_path);
  unlink(bloom_path);
  assert(WIFEXITED(ret) && WEXITSTATUS(ret) == 0);
}

int main(void) {
  expect_normalize("word\n", 5, "word", 4);
  expect_normalize("word\r\n", 6, "word", 4);
  expect_normalize("word\r", 5, "word", 4);
  expect_normalize("word", 4, "word", 4);
  /* empty-line: len==0 must not crash and must return 0 */
  {
    char buf[4] = "abc";
    size_t out = normalize_line(buf, 0);
    assert(out == 0);
    assert(buf[0] == '\0');
  }

  run_hexln_case("abc", "616263\n");
  run_hexln_case("abc\\r\\n", "616263\n");
  run_hexln_case("abc\\r", "616263\n");

  test_brainflayer_mixed_newlines();
  test_bloom_roundtrip();

  printf("OK\n");
  return 0;
}

