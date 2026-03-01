/* Copyright (c) 2015 Ryan Castellucci, All Rights Reserved */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

#ifdef _WIN32
# include "win_compat.h"
#else
# include <unistd.h>
# include <sys/time.h>
# ifdef __linux__
#  include <sys/sysinfo.h>
# endif
# include <pthread.h>
#endif

#include <openssl/sha.h>

#include "ripemd160_256.h"

#include "ec_pubkey_fast.h"

#include "hex.h"
#include "bloom.h"
#include "mmapf.h"
#include "hash160.h"
#include "hsearchf.h"

#include "algo/brainv2.h"
#include "algo/warpwallet.h"
#include "algo/brainwalletio.h"
#include "algo/sha3.h"

// raise this if you really want, but quickly diminishing returns
#define BATCH_MAX 4096
#define BATCH_DEFAULT 1024

static int brainflayer_is_init = 0;

typedef struct pubhashfn_s {
   void (*fn)(hash160_t *, const unsigned char *);
   char id;
} pubhashfn_t;

static unsigned char *mem;

static mmapf_ctx bloom_mmapf;
static unsigned char *bloom = NULL;

/* ---------- shared config (set by main before workers start) -------------- */
static FILE          *ifile      = NULL;
static FILE          *ofile      = NULL;
static FILE          *ffile      = NULL;
static int            vopt       = 0;
static int            xopt       = 0;
static int            tty        = 0;
static int            nopt_mod   = 0;
static int            nopt_rem   = 0;
static int            Bopt       = 0;
static int            g_skipping = 0;
static uint64_t       kopt       = 0;
static uint64_t       Nopt       = ~0ULL;
static unsigned char *fopt       = NULL;
static unsigned char *Iopt       = NULL;
static unsigned char  modestr[64];
static pubhashfn_t    pubhashfn[8];

/* ---------- threading state ----------------------------------------------- */
#ifndef _WIN32
static pthread_mutex_t input_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static volatile uint64_t g_ilines_curr = 0;
static volatile uint64_t g_olines      = 0;
static volatile int64_t  g_raw_lines   = -1;
static volatile int       g_eof        = 0;

typedef struct {
  int    thread_id;
  int    num_threads;
  secp256k1_batch_t *batch_ctx;
  char          *batch_line[BATCH_MAX];
  size_t         batch_line_sz[BATCH_MAX];
  size_t         batch_line_read[BATCH_MAX];
  unsigned char  batch_priv[BATCH_MAX][32];
  unsigned char  batch_upub[BATCH_MAX][65];
  unsigned char *unhexed;
  size_t         unhexed_sz;
  unsigned char  start_priv[32]; /* incremental mode: per-thread start */
  /* stats (thread 0 only, when vopt) */
  uint64_t time_start;
  uint64_t time_last;
  uint64_t ilines_last;
  float    ilines_rate_avg;
  uint64_t report_mask;
  float    alpha;
} worker_ctx_t;

#define bail(code, ...) \
do { \
  fprintf(stderr, __VA_ARGS__); \
  exit(code); \
} while (0)

#define chkmalloc(S) _chkmalloc(S, __FILE__, __LINE__)
static void * _chkmalloc(size_t size, unsigned char *file, unsigned int line) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    bail(1, "malloc(%zu) failed at %s:%u: %s\n", size, file, line, strerror(errno));
  }
  return ptr;
}

#define chkrealloc(P, S) _chkrealloc(P, S, __FILE__, __LINE__);
static void * _chkrealloc(void *ptr, size_t size, unsigned char *file, unsigned int line) {
  void *ptr2 = realloc(ptr, size);
  if (ptr2 == NULL) {
    bail(1, "realloc(%p, %zu) failed at %s:%u: %s\n", ptr, size, file, line, strerror(errno));
  }
  return ptr2;
}

uint64_t getns() {
  uint64_t ns;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  ns  = ts.tv_nsec;
  ns += ts.tv_sec * 1000000000ULL;
  return ns;
}

static inline void brainflayer_init_globals() {
  /* only initialize stuff once */
  if (!brainflayer_is_init) {
    /* initialize buffers */
    mem = chkmalloc(4096);

    /* set the flag */
    brainflayer_is_init = 1;
  }
}

// function pointers
static int (*input2priv)(unsigned char *, unsigned char *, size_t);

/* bitcoin uncompressed address */
static void uhash160(hash160_t *h, const unsigned char *upub) {
  SHA256_CTX ctx;
  unsigned char hash[SHA256_DIGEST_LENGTH];

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, upub, 65);
  SHA256_Final(hash, &ctx);
  ripemd160_256(hash, h->uc);
}

/* bitcoin compressed address */
static void chash160(hash160_t *h, const unsigned char *upub) {
  SHA256_CTX ctx;
  unsigned char cpub[33];
  unsigned char hash[SHA256_DIGEST_LENGTH];

  /* quick and dirty public key compression */
  cpub[0] = 0x02 | (upub[64] & 0x01);
  memcpy(cpub + 1, upub + 1, 32);
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, cpub, 33);
  SHA256_Final(hash, &ctx);
  ripemd160_256(hash, h->uc);
}

/* ethereum address */
static void ehash160(hash160_t *h, const unsigned char *upub) {
  SHA3_256_CTX ctx;
  unsigned char hash[SHA256_DIGEST_LENGTH];

  /* compute hash160 for uncompressed public key */
  /* keccak_256_last160(pub) */
  KECCAK_256_Init(&ctx);
  KECCAK_256_Update(&ctx, upub+1, 64);
  KECCAK_256_Final(hash, &ctx);
  memcpy(h->uc, hash+12, 20);
}

/* msb of x coordinate of public key */
static void xhash160(hash160_t *h, const unsigned char *upub) {
  memcpy(h->uc, upub+1, 20);
}



static int pass2priv(unsigned char *priv, unsigned char *pass, size_t pass_sz) {
  SHA256_CTX ctx;

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, pass, pass_sz);
  SHA256_Final(priv, &ctx);

  return 0;
}

static int keccak2priv(unsigned char *priv, unsigned char *pass, size_t pass_sz) {
  SHA3_256_CTX ctx;

  KECCAK_256_Init(&ctx);
  KECCAK_256_Update(&ctx, pass, pass_sz);
  KECCAK_256_Final(priv, &ctx);

  return 0;
}

/* ether.camp "2031 passes of SHA-3 (Keccak)" */
static int camp2priv(unsigned char *priv, unsigned char *pass, size_t pass_sz) {
  SHA3_256_CTX ctx;
  int i;

  KECCAK_256_Init(&ctx);
  KECCAK_256_Update(&ctx, pass, pass_sz);
  KECCAK_256_Final(priv, &ctx);

  for (i = 1; i < 2031; ++i) {
    KECCAK_256_Init(&ctx);
    KECCAK_256_Update(&ctx, priv, 32);
    KECCAK_256_Final(priv, &ctx);
  }

  return 0;
}

static int sha32priv(unsigned char *priv, unsigned char *pass, size_t pass_sz) {
  SHA3_256_CTX ctx;

  SHA3_256_Init(&ctx);
  SHA3_256_Update(&ctx, pass, pass_sz);
  SHA3_256_Final(priv, &ctx);

  return 0;
}

/*
static int dicap2hash160(unsigned char *pass, size_t pass_sz) {
  SHA3_256_CTX ctx;

  int i, ret;

  KECCAK_256_Init(&ctx);
  KECCAK_256_Update(&ctx, pass, pass_sz);
  KECCAK_256_Final(priv256, &ctx);
  for (i = 0; i < 16384; ++i) {
    KECCAK_256_Init(&ctx);
    KECCAK_256_Update(&ctx, priv256, 32);
    KECCAK_256_Final(priv256, &ctx);
  }

  for (;;) {
    ret = priv2hash160(priv256);
    if (hash160_uncmp.uc[0] == 0) { break; }
    KECCAK_256_Init(&ctx);
    KECCAK_256_Update(&ctx, priv256, 32);
    KECCAK_256_Final(priv256, &ctx);
  }
  return ret;
}
*/

static int rawpriv2priv(unsigned char *priv, unsigned char *rawpriv, size_t rawpriv_sz) {
  memcpy(priv, rawpriv, rawpriv_sz);
  return 0;
}

static unsigned char *kdfsalt;
static size_t kdfsalt_sz;

static int warppass2priv(unsigned char *priv, unsigned char *pass, size_t pass_sz) {
  int ret;
  if ((ret = warpwallet(pass, pass_sz, kdfsalt, kdfsalt_sz, priv)) != 0) return ret;
  pass[pass_sz] = 0;
  return 0;
}

static int bwiopass2priv(unsigned char *priv, unsigned char *pass, size_t pass_sz) {
  int ret;
  if ((ret = brainwalletio(pass, pass_sz, kdfsalt, kdfsalt_sz, priv)) != 0) return ret;
  pass[pass_sz] = 0;
  return 0;
}

static int brainv2pass2priv(unsigned char *priv, unsigned char *pass, size_t pass_sz) {
  unsigned char hexout[33];
  int ret;
  if ((ret = brainv2(pass, pass_sz, kdfsalt, kdfsalt_sz, hexout)) != 0) return ret;
  pass[pass_sz] = 0;
  return pass2priv(priv, hexout, sizeof(hexout)-1);
}

static unsigned char *kdfpass;
static size_t kdfpass_sz;

static int warpsalt2priv(unsigned char *priv, unsigned char *salt, size_t salt_sz) {
  int ret;
  if ((ret = warpwallet(kdfpass, kdfpass_sz, salt, salt_sz, priv)) != 0) return ret;
  salt[salt_sz] = 0;
  return 0;
}

static int bwiosalt2priv(unsigned char *priv, unsigned char *salt, size_t salt_sz) {
  int ret;
  if ((ret = brainwalletio(kdfpass, kdfpass_sz, salt, salt_sz, priv)) != 0) return ret;
  salt[salt_sz] = 0;
  return 0;
}

static int brainv2salt2priv(unsigned char *priv, unsigned char *salt, size_t salt_sz) {
  unsigned char hexout[33];
  int ret;
  if ((ret = brainv2(kdfpass, kdfpass_sz, salt, salt_sz, hexout)) != 0) return ret;
  salt[salt_sz] = 0;
  return pass2priv(priv, hexout, sizeof(hexout)-1);
}

static unsigned char rushchk[5];
static int rush2priv(unsigned char *priv, unsigned char *pass, size_t pass_sz) {
  SHA256_CTX ctx;
  unsigned char hash[SHA256_DIGEST_LENGTH];
  unsigned char userpasshash[SHA256_DIGEST_LENGTH*2+1];

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, pass, pass_sz);
  SHA256_Final(hash, &ctx);

  hex(hash, sizeof(hash), userpasshash, sizeof(userpasshash));

  SHA256_Init(&ctx);
  // kdfsalt should be the fragment up to the !
  SHA256_Update(&ctx, kdfsalt, kdfsalt_sz);
  SHA256_Update(&ctx, userpasshash, 64);
  SHA256_Final(priv, &ctx);

  // early exit if the checksum doesn't match
  if (memcmp(priv, rushchk, sizeof(rushchk)) != 0) { return -1; }

  return 0;
}

inline static int priv_incr(unsigned char *upub, unsigned char *priv) {
  int sz;

  secp256k1_ec_pubkey_incr(upub, &sz, priv);

  return 0;
}

inline static void priv2pub(unsigned char *upub, const unsigned char *priv) {
  int sz;

  secp256k1_ec_pubkey_create_precomp(upub, &sz, priv);
}


inline static void fprintresult(FILE *f, hash160_t *hash,
                                unsigned char compressed,
                                unsigned char *type,
                                unsigned char *input) {
  unsigned char hexed0[41];

  fprintf(f, "%s:%c:%s:%s\n",
          hex(hash->uc, 20, hexed0, sizeof(hexed0)),
          compressed,
          type,
          input);
}

/* Add a 64-bit unsigned integer to a 32-byte big-endian private key.
 * The addition wraps silently on overflow (modulo 2^256), which is
 * fine in practice because secp256k1 scalars are always reduced mod n. */
static void priv_add_uint64(unsigned char *priv, uint64_t add) {
  uint64_t carry = add;
  int i;
  for (i = 31; i >= 0 && carry; i--) {
    carry += priv[i];
    priv[i] = (unsigned char)(carry & 0xFF);
    carry >>= 8;
  }
}

/* ---------- worker thread ------------------------------------------------- */
static void *worker_run(void *arg) {
  worker_ctx_t *ctx = (worker_ctx_t *)arg;
  int i, j;
  int batch_stopped;
  hash160_t hash160;

  for (;;) {
    if (Iopt) {
      /* Incremental mode: each thread has its own starting key.
       * After each batch, advance by num_threads * Bopt * nopt_mod. */
      secp256k1_ec_pubkey_batch_incr_mt(ctx->batch_ctx, Bopt, nopt_mod,
          ctx->batch_upub, ctx->batch_priv, ctx->start_priv);
      priv_add_uint64(ctx->start_priv,
          (uint64_t)ctx->num_threads * Bopt * nopt_mod);
      batch_stopped = Bopt;
    } else {
      /* Dictionary mode: serialise reads from the shared input file. */
      batch_stopped = 0;
#ifndef _WIN32
      pthread_mutex_lock(&input_mutex);
#endif
      if (!g_eof) {
        for (i = 0; i < Bopt;) {
          ssize_t line_read = getline(&ctx->batch_line[i],
                                      &ctx->batch_line_sz[i], ifile);
          if (line_read <= -1) { g_eof = 1; break; }

          ctx->batch_line_read[i] = normalize_line(ctx->batch_line[i],
                                                   (size_t)line_read);
          if (g_skipping) {
            ++g_raw_lines;
            if (kopt && g_raw_lines < (int64_t)kopt) { continue; }
            if (nopt_mod && g_raw_lines % nopt_mod != nopt_rem) { continue; }
          }

          if (xopt) {
            if (ctx->batch_line_read[i] & 1) {
              fprintf(stderr,
                "input length %zu is not even for hex decoding, skipping\n",
                ctx->batch_line_read[i]);
              continue;
            }
            if (ctx->batch_line_read[i] / 2 > ctx->unhexed_sz) {
              ctx->unhexed_sz = ctx->batch_line_read[i];
              ctx->unhexed = chkrealloc(ctx->unhexed, ctx->unhexed_sz);
            }
            unhex((unsigned char *)ctx->batch_line[i],
                  ctx->batch_line_read[i],
                  ctx->unhexed, ctx->unhexed_sz);
            if (input2priv(ctx->batch_priv[i], ctx->unhexed,
                           ctx->batch_line_read[i] / 2) != 0) {
              fprintf(stderr, "input2priv failed! continuing...\n");
            }
          } else {
            if (input2priv(ctx->batch_priv[i],
                           (unsigned char *)ctx->batch_line[i],
                           ctx->batch_line_read[i]) != 0) {
              fprintf(stderr, "input2priv failed! continuing...\n");
            }
          }
          ++i;
        }
        batch_stopped = i;
      }
#ifndef _WIN32
      pthread_mutex_unlock(&input_mutex);
#endif

      if (batch_stopped > 0) {
        secp256k1_ec_pubkey_batch_create_mt(ctx->batch_ctx, batch_stopped,
            ctx->batch_upub, ctx->batch_priv);
      }
    }

    /* Process public keys */
    for (i = 0; i < batch_stopped; ++i) {
      if (bloom) { /* crack mode */
        for (j = 0; pubhashfn[j].fn != NULL; ++j) {
          pubhashfn[j].fn(&hash160, ctx->batch_upub[i]);
          if (!bloom_chk_hash160(bloom, hash160.ul)) { continue; }
#ifndef _WIN32
          pthread_mutex_lock(&output_mutex);
#endif
          if (!fopt || hsearchf(ffile, &hash160)) {
            if (tty) { fprintf(ofile, "\033[0K"); }
            if (Iopt) {
              hex(ctx->batch_priv[i], 32,
                  (unsigned char *)ctx->batch_line[i], 65);
            }
            fprintresult(ofile, &hash160, pubhashfn[j].id, modestr,
                         (unsigned char *)ctx->batch_line[i]);
#ifndef _WIN32
            __atomic_fetch_add(&g_olines, 1, __ATOMIC_RELAXED);
#else
            ++g_olines;
#endif
          }
#ifndef _WIN32
          pthread_mutex_unlock(&output_mutex);
#endif
        }
      } else { /* generate mode */
#ifndef _WIN32
        pthread_mutex_lock(&output_mutex);
#endif
        if (Iopt) {
          hex(ctx->batch_priv[i], 32,
              (unsigned char *)ctx->batch_line[i], 65);
        }
        j = 0;
        while (pubhashfn[j].fn != NULL) {
          pubhashfn[j].fn(&hash160, ctx->batch_upub[i]);
          fprintresult(ofile, &hash160, pubhashfn[j].id, modestr,
                       (unsigned char *)ctx->batch_line[i]);
          ++j;
        }
#ifndef _WIN32
        pthread_mutex_unlock(&output_mutex);
#endif
      }
    }
    /* end public key loop */

#ifndef _WIN32
    uint64_t ic = __atomic_add_fetch(&g_ilines_curr, batch_stopped,
                                     __ATOMIC_RELAXED);
#else
    g_ilines_curr += batch_stopped;
    uint64_t ic = g_ilines_curr;
#endif

    /* Stats (thread 0 only, mirrors original single-thread behaviour).
     * The original code incremented ilines_curr once unconditionally and
     * once more inside the vopt block, effectively doubling the counter in
     * verbose mode.  That affects the -N exit threshold when -v is active,
     * so we replicate the same pattern here to keep single-thread semantics
     * identical. */
    if (vopt && ctx->thread_id == 0) {
      /* second increment – matches original "ilines_curr += batch_stopped"
       * inside the vopt block */
#ifndef _WIN32
      ic = __atomic_add_fetch(&g_ilines_curr, batch_stopped, __ATOMIC_RELAXED);
#else
      g_ilines_curr += batch_stopped;
      ic = g_ilines_curr;
#endif
      if (batch_stopped < Bopt || (ic & ctx->report_mask) == 0) {
        uint64_t time_curr    = getns();
        uint64_t time_delta   = time_curr - ctx->time_last;
        uint64_t time_elapsed = time_curr - ctx->time_start;
        ctx->time_last        = time_curr;
        uint64_t ilines_delta = ic - ctx->ilines_last;
        ctx->ilines_last      = ic;
        float ilines_rate = (ilines_delta * 1.0e9) / (time_delta * 1.0);

        if (batch_stopped < Bopt) {
          ctx->ilines_rate_avg = (ic * 1.0e9) / (time_elapsed * 1.0);
        } else if (ctx->ilines_rate_avg < 0) {
          ctx->ilines_rate_avg = ilines_rate;
        } else if (time_delta < 2500000000ULL) {
          ctx->report_mask = (ctx->report_mask << 1) | 1;
          ctx->ilines_rate_avg = ilines_rate;
        } else if (time_delta > 10000000000ULL) {
          ctx->report_mask >>= 1;
          ctx->ilines_rate_avg = ilines_rate;
        } else {
          ctx->ilines_rate_avg = ctx->alpha * ilines_rate
                               + (1 - ctx->alpha) * ctx->ilines_rate_avg;
        }

        fprintf(stderr,
            "\033[0G\033[2K"
            " rate: %9.2f p/s"
            " found: %5zu/%-10zu"
            " elapsed: %8.3f s"
            "\033[0G",
            ctx->ilines_rate_avg,
            (size_t)g_olines,
            (size_t)ic,
            time_elapsed / 1.0e9
        );
        fflush(stderr);
      }
    }

    /* Exit condition */
    if (batch_stopped < Bopt || g_eof || ic >= Nopt) {
      if (vopt && ctx->thread_id == 0) { fprintf(stderr, "\n"); }
      break;
    }
  }

  return NULL;
}
/* -------------------------------------------------------------------------- */

void usage(unsigned char *name) {
  printf("Usage: %s [OPTION]...\n\n\
 -a                          open output file in append mode\n\
 -b FILE                     check for matches against bloom filter FILE\n\
 -f FILE                     verify matches against sorted hash160s in FILE\n\
 -i FILE                     read from FILE instead of stdin\n\
 -o FILE                     write to FILE instead of stdout\n\
 -c TYPES                    use TYPES for public key to hash160 computation\n\
                             multiple can be specified, for example the default\n\
                             is 'uc', which will check for both uncompressed\n\
                             and compressed addresses using Bitcoin's algorithm\n\
                             u - uncompressed address\n\
                             c - compressed address\n\
                             e - ethereum address\n\
                             x - most signifigant bits of x coordinate\n\
 -t TYPE                     inputs are TYPE - supported types:\n\
                             sha256 (default) - classic brainwallet\n\
                             sha3   - sha3-256\n\
                             priv   - raw private keys (requires -x)\n\
                             warp   - WarpWallet (supports -s or -p)\n\
                             bwio   - brainwallet.io (supports -s or -p)\n\
                             bv2    - brainv2 (supports -s or -p) VERY SLOW\n\
                             rush   - rushwallet (requires -r) FAST\n\
                             keccak - keccak256 (ethercamp/old ethaddress)\n\
                             camp2  - keccak256 * 2031 (new ethercamp)\n\
 -x                          treat input as hex encoded\n\
 -s SALT                     use SALT for salted input types (default: none)\n\
 -p PASSPHRASE               use PASSPHRASE for salted input types, inputs\n\
                             will be treated as salts\n\
 -r FRAGMENT                 use FRAGMENT for cracking rushwallet passphrase\n\
 -I HEXPRIVKEY               incremental private key cracking mode, starting\n\
                             at HEXPRIVKEY (supports -n) FAST\n\
 -k K                        skip the first K lines of input\n\
 -N N                        stop after N input lines or keys\n\
 -n K/N                      use only the Kth of every N input lines\n\
  -B BATCH_SIZE               batch size for affine transformations\n\
                              must be a power of 2 (default: %d, max: %d)\n\
 -w WINDOW_SIZE              window size for ecmult table (default: 16)\n\
                             uses about 3 * 2^w KiB memory on startup, but\n\
                             only about 2^w KiB once the table is built\n\
 -m FILE                     load ecmult table from FILE\n\
                             the ecmtabgen tool can build such a table\n\
 -v                          verbose - display cracking progress\n\
 -j THREADS                  number of worker threads (default: 1)\n\
  -h                          show this help\n", name, BATCH_DEFAULT, BATCH_MAX);
//q, --quiet                 suppress non-error messages
  exit(1);
}

int main(int argc, char **argv) {
  ifile = stdin;
  ofile = stdout;

  int ret, c, i;
  bool free_kdfsalt = false;

  int spok = 0, aopt = 0, wopt = 16, jopt = 1;
  unsigned char *bopt = NULL, *iopt = NULL, *oopt = NULL;
  unsigned char *topt = NULL, *sopt = NULL, *popt = NULL;
  unsigned char *mopt = NULL, *ropt = NULL, *copt = NULL;

  unsigned char priv[32];
  memset(pubhashfn, 0, sizeof(pubhashfn));

  while ((c = getopt(argc, argv, "avxb:hi:j:k:f:m:n:o:p:s:r:c:t:w:I:N:B:")) != -1) {
    switch (c) {
      case 'a':
        aopt = 1; // open output file in append mode
        break;
      case 'k':
        kopt = strtoull(optarg, NULL, 10); // skip first k lines of input
        g_skipping = 1;
        break;
      case 'n':
        // only try the rem'th of every mod lines (one indexed)
        nopt_rem = atoi(optarg) - 1;
        optarg = strchr(optarg, '/');
        if (optarg != NULL) { nopt_mod = atoi(optarg+1); }
        g_skipping = 1;
        break;
      case 'B':
        Bopt = atoi(optarg);
        break;
      case 'N':
        Nopt = strtoull(optarg, NULL, 0); // allows 0x
        break;
      case 'w':
        if (wopt > 1) wopt = atoi(optarg);
        break;
      case 'm':
        mopt = optarg; // table file
        wopt = 1; // auto
        break;
      case 'v':
        vopt = 1; // verbose
        break;
      case 'b':
        bopt = optarg; // bloom filter file
        break;
      case 'f':
        fopt = optarg; // full filter file
        break;
      case 'i':
        iopt = optarg; // input file
        break;
      case 'j':
        jopt = atoi(optarg); // number of worker threads
        break;
      case 'o':
        oopt = optarg; // output file
        break;
      case 'x':
        xopt = 1; // input is hex encoded
        break;
      case 's':
        sopt = optarg; // salt
        break;
      case 'p':
        popt = optarg; // passphrase
        break;
      case 'r':
        ropt = optarg; // rushwallet
        break;
      case 'c':
        copt = optarg; // type of hash160
        break;
      case 't':
        topt = optarg; // type of input
        break;
      case 'I':
        Iopt = optarg; // start key for incremental
        xopt = 1; // input is hex encoded
        break;
      case 'h':
        // show help
        usage(argv[0]);
        return 0;
      case '?':
        // show error
        return 1;
      default:
        // should never be reached...
        printf("got option '%c' (%d)\n", c, c);
        return 1;
    }
  }

  if (optind < argc) {
    if (optind == 1 && argc == 2) {
      // older versions of brainflayer had the bloom filter file as a
      // single optional argument, this keeps compatibility with that
      bopt = argv[1];
    } else {
      fprintf(stderr, "Invalid arguments:\n");
      while (optind < argc) {
        fprintf(stderr, "    '%s'\n", argv[optind++]);
      }
      exit(1);
    }
  }

  if (nopt_rem != 0 || nopt_mod != 0) {
    // note that nopt_rem has had one subtracted at option parsing
    if (nopt_rem >= nopt_mod) {
      bail(1, "Invalid '-n' argument, remainder '%d' must be <= modulus '%d'\n", nopt_rem+1, nopt_mod);
    } else if (nopt_rem < 0) {
      bail(1, "Invalid '-n' argument, remainder '%d' must be > 0\n", nopt_rem+1);
    } else if (nopt_mod < 1) {
      bail(1, "Invalid '-n' argument, modulus '%d' must be > 0\n", nopt_mod);
    }
  }

  if (jopt < 1) {
    bail(1, "Invalid '-j' argument, must be >= 1\n");
  }

  if (wopt < 1 || wopt > 28) {
    bail(1, "Invalid window size '%d' - must be >= 1 and <= 28\n", wopt);
  } else {
    // very rough sanity check of window size
#ifdef __linux__
    struct sysinfo info;
    sysinfo(&info);
    uint64_t sysram = (uint64_t)info.mem_unit * info.totalram;
    if (3584LLU*(1<<wopt) > sysram) {
      bail(1, "Not enough ram for requested window size '%d'\n", wopt);
    }
#elif defined(_WIN32)
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
      uint64_t sysram = ms.ullTotalPhys;
      if (3584LLU*(1<<wopt) > sysram) {
        bail(1, "Not enough ram for requested window size '%d'\n", wopt);
      }
    }
#endif
  }

  if (Bopt) { // if unset, will be set later
    if (Bopt < 1 || Bopt > BATCH_MAX) {
      bail(1, "Invalid '-B' argument, batch size '%d' - must be >= 1 and <= %d\n", Bopt, BATCH_MAX);
    } else if (Bopt & (Bopt - 1)) { // https://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
      bail(1, "Invalid '-B' argument, batch size '%d' is not a power of 2\n", Bopt);
    }
  }

  if (Iopt) {
    if (strlen(Iopt) != 64) {
      bail(1, "The starting key passed to the '-I' must be 64 hex digits exactly\n");
    }
    if (topt) {
      bail(1, "Cannot specify input type in incremental mode\n");
    }
    topt = "priv";
    unhex(Iopt, 64, priv, 32);
    g_skipping = 1;
    if (!nopt_mod) { nopt_mod = 1; };
  }


  /* handle copt */
  if (copt == NULL) { copt = "uc"; }
  i = 0;
  while (copt[i]) {
    switch (copt[i]) {
      case 'u':
        pubhashfn[i].fn = &uhash160;
        break;
      case 'c':
        pubhashfn[i].fn = &chash160;
        break;
      case 'e':
        pubhashfn[i].fn = &ehash160;
        break;
      case 'x':
        pubhashfn[i].fn = &xhash160;
        break;
      default:
        bail(1, "Unknown hash160 type '%c'.\n", copt[i]);
    }
    if (strchr(copt + i + 1, copt[i])) {
      bail(1, "Duplicate hash160 type '%c'.\n", copt[i]);
    }
    pubhashfn[i].id = copt[i];
    ++i;
  }

  /* handle topt */
  if (topt == NULL) { topt = "sha256"; }

  if (strcmp(topt, "sha256") == 0) {
    input2priv = &pass2priv;
  } else if (strcmp(topt, "priv") == 0) {
    if (!xopt) {
      bail(1, "raw private key input requires -x");
    }
    input2priv = &rawpriv2priv;
  } else if (strcmp(topt, "warp") == 0) {
    if (!Bopt) { Bopt = 1; } // don't batch transform for slow input hashes by default
    spok = 1;
    input2priv = popt ? &warpsalt2priv : &warppass2priv;
  } else if (strcmp(topt, "bwio") == 0) {
    if (!Bopt) { Bopt = 1; } // don't batch transform for slow input hashes by default
    spok = 1;
    input2priv = popt ? &bwiosalt2priv : &bwiopass2priv;
  } else if (strcmp(topt, "bv2") == 0) {
    if (!Bopt) { Bopt = 1; } // don't batch transform for slow input hashes by default
    spok = 1;
    input2priv = popt ? &brainv2salt2priv : &brainv2pass2priv;
  } else if (strcmp(topt, "rush") == 0) {
    input2priv = &rush2priv;
  } else if (strcmp(topt, "camp2") == 0) {
    input2priv = &camp2priv;
  } else if (strcmp(topt, "keccak") == 0) {
    input2priv = &keccak2priv;
  } else if (strcmp(topt, "sha3") == 0) {
    input2priv = &sha32priv;
//  } else if (strcmp(topt, "dicap") == 0) {
//    input2priv = &dicap2priv;
  } else {
    bail(1, "Unknown input type '%s'.\n", topt);
  }

  if (spok) {
    if (sopt && popt) {
      bail(1, "Cannot specify both a salt and a passphrase\n");
    }
    if (popt) {
      kdfpass = popt;
      kdfpass_sz = strlen(popt);
    } else {
      if (sopt) {
        kdfsalt = sopt;
        kdfsalt_sz = strlen(kdfsalt);
      } else {
        kdfsalt = chkmalloc(0);
        kdfsalt_sz = 0;
        free_kdfsalt = true;
      }
    }
  } else {
    if (popt) {
      bail(1, "Specifying a passphrase not supported with input type '%s'\n", topt);
    } else if (sopt) {
      bail(1, "Specifying a salt not supported with this input type '%s'\n", topt);
    }
  }

  if (ropt) {
    if (input2priv != &rush2priv) {
      bail(1, "Specifying a url fragment only supported with input type 'rush'\n");
    }
    kdfsalt = ropt;
    kdfsalt_sz = strlen(kdfsalt) - sizeof(rushchk)*2;
    if (kdfsalt[kdfsalt_sz-1] != '!') {
      bail(1, "Invalid rushwallet url fragment '%s'\n", kdfsalt);
    }
    unhex(kdfsalt+kdfsalt_sz, sizeof(rushchk)*2, rushchk, sizeof(rushchk));
    kdfsalt[kdfsalt_sz] = '\0';
  } else if (input2priv == &rush2priv) {
    bail(1, "The '-r' option is required for rushwallet.\n");
  }

  snprintf(modestr, sizeof(modestr), xopt ? "(hex)%s" : "%s", topt);

  if (bopt) {
    if ((ret = mmapf(&bloom_mmapf, bopt, BLOOM_SIZE, MMAPF_RNDRD)) != MMAPF_OKAY) {
      bail(1, "failed to open bloom filter '%s': %s\n", bopt, mmapf_strerror(ret));
    } else if (bloom_mmapf.mem == NULL) {
      bail(1, "got NULL pointer trying to set up bloom filter\n");
    }
    bloom = bloom_mmapf.mem;
  }

  if (fopt) {
    if (!bopt) {
      bail(1, "The '-f' option must be used with a bloom filter\n");
    }
    if ((ffile = fopen(fopt, "r")) == NULL) {
      bail(1, "failed to open '%s' for reading: %s\n", fopt, strerror(errno));
    }
  }

  if (iopt) {
    if ((ifile = fopen(iopt, "r")) == NULL) {
      bail(1, "failed to open '%s' for reading: %s\n", iopt, strerror(errno));
    }
    // increases readahead window, don't really care if it fails
    posix_fadvise(fileno(ifile), 0, 0, POSIX_FADV_SEQUENTIAL);
  }

  if (oopt && (ofile = fopen(oopt, (aopt ? "a" : "w"))) == NULL) {
    bail(1, "failed to open '%s' for writing: %s\n", oopt, strerror(errno));
  }

  /* line buffer output */
  setvbuf(ofile,  NULL, _IOLBF, 0);
  /* line buffer stderr */
  setvbuf(stderr, NULL, _IOLBF, 0);

  if (vopt && ofile == stdout && isatty(fileno(stdout))) { tty = 1; }

  brainflayer_init_globals();

  if (secp256k1_ec_pubkey_precomp_table(wopt, mopt) != 0) {
    bail(1, "failed to initialize precomputed table\n");
  }

  // set default batch size
  if (!Bopt) { Bopt = BATCH_DEFAULT; }

  /* Allocate and initialise per-worker contexts */
  worker_ctx_t *workers = chkmalloc(jopt * sizeof(worker_ctx_t));
  memset(workers, 0, jopt * sizeof(worker_ctx_t));

  for (i = 0; i < jopt; ++i) {
    int k;
    workers[i].thread_id   = i;
    workers[i].num_threads = jopt;

    if (secp256k1_ec_pubkey_batch_alloc(&workers[i].batch_ctx, BATCH_MAX) != 0) {
      bail(1, "failed to allocate batch context for thread %d\n", i);
    }

    workers[i].unhexed_sz = 4096;
    workers[i].unhexed    = chkmalloc(workers[i].unhexed_sz);

    if (Iopt) {
      /* Pre-allocate output buffers for hex private key formatting */
      for (k = 0; k < BATCH_MAX; ++k) {
        workers[i].batch_line[k]    = chkmalloc(65);
        workers[i].batch_line_sz[k] = 65;
      }
      /* Compute per-thread starting key:
       *   thread t starts at base + (nopt_rem + kopt) steps,
       *   then additionally t * Bopt * nopt_mod steps ahead. */
      memcpy(workers[i].start_priv, priv, 32);
      priv_add_uint64(workers[i].start_priv, (uint64_t)nopt_rem + kopt);
      if (i > 0) {
        priv_add_uint64(workers[i].start_priv, (uint64_t)i * Bopt * nopt_mod);
      }
    }

    if (i == 0 && vopt) {
      workers[i].time_start      = workers[i].time_last = getns();
      workers[i].ilines_last     = 0;
      workers[i].ilines_rate_avg = -1;
      workers[i].report_mask     = 0;
      workers[i].alpha           = 0.500f;
    }
  }

  /* Dispatch workers */
#ifndef _WIN32
  if (jopt > 1) {
    pthread_t *threads = chkmalloc((jopt - 1) * sizeof(pthread_t));
    for (i = 1; i < jopt; ++i) {
      if (pthread_create(&threads[i-1], NULL, worker_run, &workers[i]) != 0) {
        bail(1, "failed to create thread %d: %s\n", i, strerror(errno));
      }
    }
    worker_run(&workers[0]); /* run thread 0 on the calling stack */
    for (i = 1; i < jopt; ++i) {
      pthread_join(threads[i-1], NULL);
    }
    free(threads);
  } else
#endif
  {
    worker_run(&workers[0]);
  }

  /* Free per-worker resources */
  for (i = 0; i < jopt; ++i) {
    int k;
    secp256k1_ec_pubkey_batch_dealloc(workers[i].batch_ctx);
    free(workers[i].unhexed);
    for (k = 0; k < BATCH_MAX; ++k) {
      /* In dict mode batch_line[k] is allocated by getline() and may be NULL
       * if the thread was created but never read a line; free(NULL) is safe. */
      free(workers[i].batch_line[k]);
    }
  }
  free(workers);

  if (bloom_mmapf.mem) {
    munmapf(&bloom_mmapf);
    bloom = NULL;
  }
  if (ffile) { fclose(ffile); }
  if (ifile && ifile != stdin) { fclose(ifile); }
  if (ofile && ofile != stdout) { fclose(ofile); }
  if (free_kdfsalt && kdfsalt) {
    free(kdfsalt);
    kdfsalt = NULL;
  }
  free(mem);
  mem = NULL;
  secp256k1_ec_pubkey_batch_free();
  secp256k1_ec_pubkey_precomp_table_free();
  return 0;
}

/*  vim: set ts=2 sw=2 et ai si: */
