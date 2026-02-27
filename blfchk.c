/* Copyright (c) 2015 Ryan Castellucci, All Rights Reserved */
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#ifdef _WIN32
# include "win_compat.h"
#else
# include <sys/time.h>
#endif

#include "hex.h"
#include "bloom.h"
#include "mmapf.h"
#include "hash160.h"
#include "hsearchf.h"

int main(int argc, char **argv) {
  int ret;
  hash160_t hash;
  char *line = NULL;
  size_t line_sz = 0;
  ssize_t line_read;
  unsigned char *bloom, *bloomfile = NULL, *hashfile = NULL;
  char *ifilename = NULL;
  FILE *ifile = stdin, *ofile = stdout, *hfile = NULL;
  mmapf_ctx bloom_mmapf;
  int opt;

#define USAGE() fprintf(stderr, \
  "Usage: %s [-i INPUT_FILE] BLOOM_FILTER_FILE [HASH_FILE]\n", argv[0])

  while ((opt = getopt(argc, argv, "i:")) != -1) {
    switch (opt) {
      case 'i': ifilename = optarg; break;
      default:
        USAGE();
        return 1;
    }
  }

  if (optind >= argc || argc - optind > 2) {
    USAGE();
    return 1;
  }

  bloomfile = (unsigned char *)argv[optind];
  if (argc - optind == 2) { hashfile = (unsigned char *)argv[optind + 1]; }

  if (ifilename != NULL) {
    if ((ifile = fopen(ifilename, "r")) == NULL) {
      fprintf(stderr, "failed to open input file '%s': %s\n", ifilename, strerror(errno));
      return 1;
    }
  }

  if ((ret = mmapf(&bloom_mmapf, bloomfile, BLOOM_SIZE, MMAPF_RNDRD)) != MMAPF_OKAY) {
    fprintf(stderr, "failed to open bloom filter '%s': %s\n", bloomfile, mmapf_strerror(ret));
    if (ifile != stdin) { fclose(ifile); }
    return 1;
  } else if (bloom_mmapf.mem == NULL) {
    fprintf(stderr, "got NULL pointer trying to set up bloom filter\n");
    if (ifile != stdin) { fclose(ifile); }
    return 1;
  }

  bloom = bloom_mmapf.mem;

  if (hashfile != NULL) {
    hfile = fopen((char *)hashfile, "r");
  }

  while ((line_read = getline(&line, &line_sz, ifile)) > 0) {
    size_t len = normalize_line(line, (size_t)line_read);
    if (len != sizeof(hash.uc) * 2) { continue; }
    unhex((unsigned char *)line, len, hash.uc, sizeof(hash.uc));

    unsigned int bit;
    bit = BH00(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH01(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH02(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH03(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH04(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH05(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH06(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH07(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH08(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH09(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH10(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH11(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH12(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH13(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH14(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH15(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH16(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH17(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH18(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }
    bit = BH19(hash.ul); if (BLOOM_GET_BIT(bit) == 0) { continue; }

    if (hfile && !hsearchf(hfile, &hash)) {
      //fprintf(ofile, "%s (false positive)\n", hex(hash.uc, sizeof(hash.uc), buf, sizeof(buf)));
      continue;
    }
    //fprintf(ofile, "%s\n", hex(hash.uc, sizeof(hash.uc), buf, sizeof(buf)));
    fprintf(ofile, "%s\n", line);
  }

  if (hfile) { fclose(hfile); }
  if (ifile != stdin) { fclose(ifile); }
  munmapf(&bloom_mmapf);
  free(line);
  return 0;
}
