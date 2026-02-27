/* Copyright (c) 2024, All Rights Reserved
 * addr2hex — convert Bitcoin (and compatible) Base58Check addresses
 * to hex-encoded hash160 for use with hex2blf / bloom filter creation.
 *
 * Supports P2PKH (version 0x00, starts with '1') and
 *          P2SH  (version 0x05, starts with '3') addresses,
 * as well as altcoin addresses that use the same Base58Check format.
 *
 * Usage:
 *   addr2hex [addressfile.txt]
 *   Reads one address per line from the file (or stdin if omitted).
 *   Outputs the 40-character hex-encoded hash160, one per line.
 *   Invalid or malformed addresses are silently skipped (warning to stderr).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <openssl/sha.h>

#include "hex.h"

/* 1 version byte + 20 hash160 bytes + 4 checksum bytes */
#define ADDR_DECODED_LEN 25
#define HASH160_LEN      20

/*
 * Lookup table: ASCII → Base58 digit value (-1 = invalid character).
 * Base58 alphabet: 123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz
 */
static const int8_t b58_tab[256] = {
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-1,-1,-1,-1,-1,-1,  /* '1'-'9' */
  -1, 9,10,11,12,13,14,15,16,-1,17,18,19,20,21,-1,  /* 'A'-'Z' (no 'I','O') */
  22,23,24,25,26,27,28,29,30,31,32,-1,-1,-1,-1,-1,
  -1,33,34,35,36,37,38,39,40,41,42,43,-1,44,45,46,  /* 'a'-'z' (no 'l') */
  47,48,49,50,51,52,53,54,55,56,57,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

/*
 * Decode a Base58-encoded string into raw bytes.
 * Returns the number of bytes written to out[], or -1 on error.
 * out[] must be at least out_sz bytes.  Leading '1' chars map to 0x00 bytes.
 */
static int base58_decode(const char *str, uint8_t *out, size_t out_sz) {
  uint8_t buf[64];
  int i, j, zeros = 0;

  memset(buf, 0, sizeof(buf));

  /* Count leading '1' characters (they encode leading 0x00 bytes) */
  while (*str == '1') { zeros++; str++; }

  /* Process each Base58 character */
  while (*str) {
    int8_t digit = b58_tab[(unsigned char)*str++];
    if (digit < 0) return -1; /* invalid character */
    int carry = digit;
    for (j = (int)sizeof(buf) - 1; j >= 0; j--) {
      carry += 58 * (int)buf[j];
      buf[j] = (uint8_t)(carry & 0xff);
      carry >>= 8;
    }
    if (carry) return -1; /* overflow — input too long */
  }

  /* Find the first non-zero byte in buf */
  i = 0;
  while (i < (int)sizeof(buf) && buf[i] == 0) i++;

  int payload = (int)sizeof(buf) - i;
  int total   = zeros + payload;
  if (total > (int)out_sz) return -1;

  memset(out, 0, (size_t)zeros);
  memcpy(out + zeros, buf + i, (size_t)payload);
  return total;
}

/*
 * Verify the Base58Check 4-byte checksum.
 * data must be ADDR_DECODED_LEN bytes: [version | hash160 | checksum].
 * Returns 1 on success, 0 on failure.
 */
static int base58check_verify(const uint8_t data[ADDR_DECODED_LEN]) {
  uint8_t hash1[SHA256_DIGEST_LENGTH];
  uint8_t hash2[SHA256_DIGEST_LENGTH];
  SHA256(data, ADDR_DECODED_LEN - 4, hash1);
  SHA256(hash1, SHA256_DIGEST_LENGTH, hash2);
  return memcmp(hash2, data + ADDR_DECODED_LEN - 4, 4) == 0;
}

/*
 * Process one address string (may include trailing newline/CR).
 * Prints the hex-encoded hash160 to stdout, or a warning to stderr.
 */
static void process_address(const char *line) {
  char addr[128];
  size_t len = strlen(line);
  uint8_t decoded[ADDR_DECODED_LEN];
  unsigned char hexbuf[HASH160_LEN * 2 + 1];

  /* Strip trailing whitespace */
  if (len >= sizeof(addr)) {
    fprintf(stderr, "[!] Address too long, skipping\n");
    return;
  }
  memcpy(addr, line, len);
  addr[len] = '\0';
  while (len > 0 && (addr[len-1] == '\n' || addr[len-1] == '\r' ||
                     addr[len-1] == ' '  || addr[len-1] == '\t')) {
    addr[--len] = '\0';
  }
  if (len == 0) return; /* skip blank lines */

  if (base58_decode(addr, decoded, sizeof(decoded)) != ADDR_DECODED_LEN) {
    fprintf(stderr, "[!] Invalid address (decode failed): %s\n", addr);
    return;
  }

  if (!base58check_verify(decoded)) {
    fprintf(stderr, "[!] Invalid address (bad checksum): %s\n", addr);
    return;
  }

  /* decoded[0]    = version byte (0x00 P2PKH, 0x05 P2SH, etc.)
   * decoded[1..20] = hash160
   * decoded[21..24] = checksum (already verified)
   */
  hex(decoded + 1, HASH160_LEN, hexbuf, sizeof(hexbuf));
  printf("%s\n", hexbuf);
}

int main(int argc, char **argv) {
  FILE *in = stdin;
  char *line = NULL;
  size_t line_sz = 0;
  ssize_t n;

  if (argc > 2) {
    fprintf(stderr, "Usage: %s [addressfile.txt]\n", argv[0]);
    fprintf(stderr, "  Reads Bitcoin addresses (one per line) from file or stdin.\n");
    fprintf(stderr, "  Outputs hex-encoded hash160, one per line.\n");
    return 1;
  }

  if (argc == 2) {
    in = fopen(argv[1], "r");
    if (!in) {
      perror(argv[1]);
      return 1;
    }
  }

  while ((n = getline(&line, &line_sz, in)) > 0) {
    process_address(line);
  }

  free(line);
  if (in != stdin) fclose(in);
  return 0;
}

/*  vim: set ts=2 sw=2 et ai si: */
