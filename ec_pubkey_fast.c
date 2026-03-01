/* Copyright (c) 2015 Nicolas Courtois, Guangyan Song, Ryan Castellucci, All Rights Reserved */
#include "ec_pubkey_fast.h"

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#ifndef _WIN32
# include <sys/mman.h>
#endif

#include "secp256k1/src/libsecp256k1-config.h"
#include "secp256k1/include/secp256k1.h"

#include "secp256k1/src/util.h"
#include "secp256k1/src/num_impl.h"
#include "secp256k1/src/field_impl.h"
#include "secp256k1/src/scalar_impl.h"
#include "secp256k1/src/group_impl.h"
#include "secp256k1/src/ecmult_gen_impl.h"
#include "secp256k1/src/ecmult.h"
#include "secp256k1/src/eckey_impl.h"

void secp256k1_ecmult(const secp256k1_ecmult_context_t *ctx, secp256k1_gej_t *r, const secp256k1_gej_t *a, const secp256k1_scalar_t *na, const secp256k1_scalar_t *ng) {
  fprintf(stderr, "there is no secp256k1_ecmult %p %p %p %p %p\n", (void*)ctx, (void*)r, (void*)a, (void*)na, (void*)ng);
  abort();
}

static int secp256k1_eckey_pubkey_parse(secp256k1_ge_t *elem, const unsigned char *pub, int size);

#include "mmapf.h"

#undef ASSERT

#define READBIT(A, B) ((A >> (B & 7)) & 1)
#define SETBIT(T, B, V) (T = (T & ~(1<<(B))) | ((V)<<(B)))

/* Extract a window of window_size bits starting at bit_offset from a 32-byte
 * big-endian key.  Bit 0 is the LSB of key[31], bit 255 is the MSB of
 * key[0].  No heap allocation; replaces the unsigned char a[256] expansion.
 *
 * @param key        32-byte big-endian scalar (private key or half-scalar).
 * @param bit_offset Starting bit index (0 = LSB of key[31]).
 * @param window_size Number of consecutive bits to extract (≤ 32).
 * @return           Extracted bits packed into bits 0..(window_size-1).
 */
static inline int extract_window_bits(const unsigned char *key, int bit_offset, int window_size) {
  int bits = 0;
  for (int i = 0; i < window_size; i++) {
    int global_bit = bit_offset + i;
    int byte_idx   = 31 - (global_bit >> 3);
    int bit_idx    = global_bit & 7;
    bits |= ((key[byte_idx] >> bit_idx) & 1) << i;
  }
  return bits;
}
int n_windows = 0;
int n_values;
secp256k1_gej_t nums_gej;
secp256k1_ge_t *prec;
int remmining = 0;
int WINDOW_SIZE = 0;
size_t MMAP_SIZE;
mmapf_ctx prec_mmapf;
#ifdef USE_ENDOMORPHISM
int n_half = 0;         /* windows per half-scalar (GLV decomposition) */
int remmining_half = 0; /* 128 % WINDOW_SIZE */
#endif

int secp256k1_ec_pubkey_precomp_table_save(int window_size, unsigned char *filename) {
  int fd, ret;
  size_t records;
  FILE *dest;

  if ((ret = secp256k1_ec_pubkey_precomp_table(window_size, NULL)) < 0)
    return ret;

  if ((fd = open(filename, O_RDWR | O_CREAT | O_EXCL, 0660)) < 0)
    return fd;

  records = n_windows*n_values;
  dest = fdopen(fd, "w");
  if (fwrite(prec, sizeof(secp256k1_ge_t), n_windows*n_values, dest) != records) {
    fclose(dest);
    return -1;
  }

  fclose(dest);
  return 0;
}

int secp256k1_ec_pubkey_precomp_table(int window_size, unsigned char *filename) {
  int ret;
  struct stat sb;
  size_t prec_sz;
  secp256k1_gej_t gj; // base point in jacobian coordinates
  secp256k1_gej_t *table;

  if (filename) {
    if (stat(filename, &sb) == 0) {
      if (!S_ISREG(sb.st_mode))
        return -100;
    } else {
      return -101;
    }
  }

  // try to find a window size that matched the file size
  for (;;) {
    WINDOW_SIZE = window_size;
    n_values = 1 << window_size;
#ifdef USE_ENDOMORPHISM
    /* GLV: each scalar is ~128 bits; use two half-tables */
    if (128 % window_size == 0) {
      n_half = (128 / window_size);
    } else {
      n_half = (128 / window_size) + 1;
    }
    remmining_half = 128 % window_size;
    n_windows = 2 * n_half;
    remmining = remmining_half;
#else
    if (256 % window_size == 0) {
      n_windows = (256 / window_size);
    } else {
      n_windows = (256 / window_size) + 1;
    }
    remmining = 256 % window_size;
#endif
    prec_sz = n_windows*n_values*sizeof(secp256k1_ge_t);
    if (!filename || sb.st_size <= prec_sz)
      break;
    ++window_size;
  }

  if ((ret = mmapf(&prec_mmapf, filename, prec_sz, MMAPF_RNDRD)) != MMAPF_OKAY) {
    fprintf(stderr, "failed to open ecmult table '%s': %s\n", filename, mmapf_strerror(ret));
    exit(1);
  } else if (prec_mmapf.mem == NULL) {
    fprintf(stderr, "got NULL pointer from mmapf\n");
    exit(1);
  }
  prec = prec_mmapf.mem;

  if (filename) { return 0; }

#ifdef USE_ENDOMORPHISM
  /* GLV: build only n_half windows for G table, then apply endomorphism for lambda*G table */
  table = malloc(n_half*n_values*sizeof(secp256k1_gej_t));
#else
  table = malloc(n_windows*n_values*sizeof(secp256k1_gej_t));
#endif

  if (table == NULL) {
    secp256k1_ec_pubkey_precomp_table_free();
    return -1;
  }

  secp256k1_gej_set_ge(&gj, &secp256k1_ge_const_g);

  //fprintf(stderr, "%d %d %d %d %zu\n", window_size, n_windows, n_values, remmining, prec_sz);

  static const unsigned char nums_b32[33] = "The scalar for this x is unknown";
  secp256k1_fe_t nums_x;
  secp256k1_ge_t nums_ge;
  VERIFY_CHECK(secp256k1_fe_set_b32(&nums_x, nums_b32));
  VERIFY_CHECK(secp256k1_ge_set_xo_var(&nums_ge, &nums_x, 0));
  secp256k1_gej_set_ge(&nums_gej, &nums_ge);
  /* Add G to make the bits in x uniformly distributed. */
  secp256k1_gej_add_ge_var(&nums_gej, &nums_gej, &secp256k1_ge_const_g, NULL);

  secp256k1_gej_t gbase;
  secp256k1_gej_t numsbase;
  gbase = gj; /* (2^w_size)^num_of_windows * G */
  numsbase = nums_gej; /* 2^num_of_windows * nums. */

#ifdef USE_ENDOMORPHISM
  /* Build G table: n_half windows, numsbase sums to 0 */
  for (int j = 0; j < n_half; j++) {
    table[j*n_values] = numsbase;
    for (int i = 1; i < n_values; i++) {
      secp256k1_gej_add_var(&table[j*n_values + i], &table[j*n_values + i - 1], &gbase, NULL);
    }
    for (int i = 0; i < window_size; i++) {
      secp256k1_gej_double_var(&gbase, &gbase, NULL);
    }
    secp256k1_gej_double_var(&numsbase, &numsbase, NULL);
    if (j == n_half-2) {
      secp256k1_gej_neg(&numsbase, &numsbase);
      secp256k1_gej_add_var(&numsbase, &numsbase, &nums_gej, NULL);
    }
  }
  /* Convert G table to affine (first n_half windows of prec) */
  secp256k1_ge_set_all_gej_var(n_half*n_values, prec, table, 0);

  /* Build lambda*G table: apply endomorphism to G table entries */
  for (int j = 0; j < n_half; j++) {
    for (int i = 0; i < n_values; i++) {
      secp256k1_ge_mul_lambda(&prec[(n_half+j)*n_values + i], &prec[j*n_values + i]);
    }
  }
#else
  for (int j = 0; j < n_windows; j++) {
    //[number of windows][each value from 0 - (2^window_size - 1)]
    table[j*n_values] = numsbase;
    for (int i = 1; i < n_values; i++) {
      secp256k1_gej_add_var(&table[j*n_values + i], &table[j*n_values + i - 1], &gbase, NULL);
    }

    for (int i = 0; i < window_size; i++) {
      secp256k1_gej_double_var(&gbase, &gbase, NULL);
    }
    /* Multiply numbase by 2. */
    secp256k1_gej_double_var(&numsbase, &numsbase, NULL);
    if (j == n_windows-2) {
      /* In the last iteration, numsbase is (1 - 2^j) * nums instead. */
      secp256k1_gej_neg(&numsbase, &numsbase);
      secp256k1_gej_add_var(&numsbase, &numsbase, &nums_gej, NULL);
    }
  }
  secp256k1_ge_set_all_gej_var(n_windows*n_values, prec, table, 0);
#endif

  free(table);
  return 0;
}

#ifdef USE_ENDOMORPHISM
static void secp256k1_ecmult_gen2(secp256k1_gej_t *r, const unsigned char *seckey){
  secp256k1_scalar_t k, k1, k2;
  int sign1, sign2;
  unsigned char b1[32], b2[32];

  secp256k1_scalar_set_b32(&k, seckey, NULL);
  secp256k1_scalar_split_lambda(&k1, &k2, &k);

  /* Ensure k1, k2 are positive (negate if > n/2) */
  sign1 = secp256k1_scalar_is_high(&k1);
  if (sign1) secp256k1_scalar_negate(&k1, &k1);
  sign2 = secp256k1_scalar_is_high(&k2);
  if (sign2) secp256k1_scalar_negate(&k2, &k2);

  secp256k1_scalar_get_b32(b1, &k1);
  secp256k1_scalar_get_b32(b2, &k2);

  secp256k1_gej_t r1, r2;
  r1.infinity = 1;
  r2.infinity = 1;

  for (int j = 0; j < n_half; j++) {
    int w = (j == n_half-1 && remmining_half != 0) ? remmining_half : WINDOW_SIZE;

    int bits1 = extract_window_bits(b1, j * WINDOW_SIZE, w);
    secp256k1_gej_add_ge_var(&r1, &r1, &prec[j*n_values + bits1], NULL);

    int bits2 = extract_window_bits(b2, j * WINDOW_SIZE, w);
    secp256k1_gej_add_ge_var(&r2, &r2, &prec[(n_half+j)*n_values + bits2], NULL);
  }

  if (sign1) secp256k1_gej_neg(&r1, &r1);
  if (sign2) secp256k1_gej_neg(&r2, &r2);
  secp256k1_gej_add_var(r, &r1, &r2, NULL);
}
#else
static void secp256k1_ecmult_gen2(secp256k1_gej_t *r, const unsigned char *seckey){
  r->infinity = 1;

  for (int j = 0; j < n_windows; j++) {
    int w    = (j == n_windows - 1 && remmining != 0) ? remmining : WINDOW_SIZE;
    int bits = extract_window_bits(seckey, j * WINDOW_SIZE, w);
    secp256k1_gej_add_ge_var(r, r, &prec[j*n_values + bits], NULL);
  }
}
#endif

#ifdef USE_BL_ARITHMETIC
static void secp256k1_gej_add_ge_bl(secp256k1_gej_t *r, const secp256k1_gej_t *a, const secp256k1_ge_t *b, secp256k1_fe_t *rzr) {
  secp256k1_fe_t z1z1, /*z1,*/ u2, x1, y1, t0, s2, h, hh, i, j, t1, rr,  v, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11;
  // 7M + 4S + 2 normalize + 22 mul_int/add/negate
  if (a->infinity) {
    VERIFY_CHECK(rzr == NULL);
    secp256k1_gej_set_ge(r, b);
    return;
  }
  if (b->infinity) {
    if (rzr) {
      secp256k1_fe_set_int(rzr, 1);
    }
    *r = *a;
    return;
  }
  r->infinity = 0;

  x1 = a->x; secp256k1_fe_normalize_weak(&x1);
  y1 = a->y; secp256k1_fe_normalize_weak(&y1);

  secp256k1_fe_sqr(&z1z1, &a->z);                                     // z1z1 = z1^2
  secp256k1_fe_mul(&u2, &b->x, &z1z1);                                // u2 = x2*z1z1
  secp256k1_fe_mul(&t0, &a->z, &z1z1);                                // t0 = z1*z1z1
  secp256k1_fe_mul(&s2, &b->y, &t0);                                  // s2 = y2 * t0
  secp256k1_fe_negate(&h, &x1, 1); secp256k1_fe_add(&h, &u2);         // h = u2-x1  (3)
  secp256k1_fe_sqr(&hh,&h);                                           // hh = h^2
  i = hh; secp256k1_fe_mul_int(&i,4);                                 // i = 4*hh
  if (secp256k1_fe_normalizes_to_zero_var(&h)) {
    if (secp256k1_fe_normalizes_to_zero_var(&i)) {
      secp256k1_gej_double_var(r, a, rzr);
    } else {
      if (rzr) {
        secp256k1_fe_set_int(rzr, 0);
      }
      r->infinity = 1;
    }
    return;
  }
  secp256k1_fe_mul(&j,&h,&i);                                         // j = h*i
  secp256k1_fe_negate(&t1, &y1, 1); secp256k1_fe_add(&t1, &s2);       // t1 = s2-y1
  rr = t1; secp256k1_fe_mul_int(&rr, 2);                              // rr = 2 * t1;
  secp256k1_fe_mul(&v, &x1, &i);                                      // v = x1 * i
  secp256k1_fe_sqr(&t2, &rr);                                         // t2 = rr^2
  t3 = v; secp256k1_fe_mul_int(&t3, 2);                               // t3 = 2*v
  secp256k1_fe_negate(&t4, &j, 1);   secp256k1_fe_add(&t4, &t2);      // t4 = t2 - j
  secp256k1_fe_negate(&r->x, &t3, 2); secp256k1_fe_add(&r->x, &t4);   // x3 = t4 - t3;
  //secp256k1_fe_normalize_weak(&r->x);
  secp256k1_fe_negate(&t5, &r->x, 6); secp256k1_fe_add(&t5, &v);      // t5 = v - x3
  secp256k1_fe_mul(&t6,&y1,&j);                                       // t6 = y1 * j
  t7 = t6; secp256k1_fe_mul_int(&t7,2);                               // t7 = 2*t6;
  secp256k1_fe_mul(&t8,&rr,&t5);                                      // t8 = rr* t5;
  secp256k1_fe_negate(&r->y, &t7, 2); secp256k1_fe_add(&r->y,&t8);    // y3 = t8-t7
  //secp256k1_fe_normalize_weak(&r->y);
  t9 = h; secp256k1_fe_add(&t9, &a->z);                               // t9 = z1 + h
  secp256k1_fe_sqr(&t10, &t9);                                        // t10 = t9^2
  secp256k1_fe_negate(&t11, &z1z1, 1); secp256k1_fe_add(&t11, &t10);  // t11 = t10-z1z1
  secp256k1_fe_negate(&r->z, &hh, 1); secp256k1_fe_add(&r->z, &t11);  // z3 = t11 - hh

}

#ifdef USE_ENDOMORPHISM
static void secp256k1_ecmult_gen_bl(secp256k1_gej_t *r, const unsigned char *seckey){
  secp256k1_scalar_t k, k1, k2;
  int sign1, sign2;
  unsigned char b1[32], b2[32];

  secp256k1_scalar_set_b32(&k, seckey, NULL);
  secp256k1_scalar_split_lambda(&k1, &k2, &k);

  sign1 = secp256k1_scalar_is_high(&k1);
  if (sign1) secp256k1_scalar_negate(&k1, &k1);
  sign2 = secp256k1_scalar_is_high(&k2);
  if (sign2) secp256k1_scalar_negate(&k2, &k2);

  secp256k1_scalar_get_b32(b1, &k1);
  secp256k1_scalar_get_b32(b2, &k2);

  secp256k1_gej_t r1, r2;
  r1.infinity = 1;
  r2.infinity = 1;

  for (int j = 0; j < n_half; j++) {
    int w = (j == n_half-1 && remmining_half != 0) ? remmining_half : WINDOW_SIZE;

    int bits1 = extract_window_bits(b1, j * WINDOW_SIZE, w);
    secp256k1_gej_add_ge_bl(&r1, &r1, &prec[j*n_values + bits1], NULL);

    int bits2 = extract_window_bits(b2, j * WINDOW_SIZE, w);
    secp256k1_gej_add_ge_bl(&r2, &r2, &prec[(n_half+j)*n_values + bits2], NULL);
  }

  if (sign1) secp256k1_gej_neg(&r1, &r1);
  if (sign2) secp256k1_gej_neg(&r2, &r2);
  secp256k1_gej_add_var(r, &r1, &r2, NULL);
}
#else
static void secp256k1_ecmult_gen_bl(secp256k1_gej_t *r, const unsigned char *seckey){
  r->infinity = 1;

  for (int j = 0; j < n_windows; j++) {
    int w    = (j == n_windows - 1 && remmining != 0) ? remmining : WINDOW_SIZE;
    int bits = extract_window_bits(seckey, j * WINDOW_SIZE, w);
    secp256k1_gej_add_ge_bl(r, r, &prec[j*n_values + bits], NULL);
  }
}
#endif
#endif

int secp256k1_ec_pubkey_create_precomp(unsigned char * restrict pub_chr, int * restrict pub_chr_sz, const unsigned char * restrict seckey) {
  secp256k1_gej_t pj;
  secp256k1_ge_t p;

#ifdef USE_BL_ARITHMETIC
  secp256k1_ecmult_gen_bl(&pj, seckey);
#else
  secp256k1_ecmult_gen2(&pj, seckey);
#endif
  secp256k1_ge_set_gej(&p, &pj);

  *pub_chr_sz = 65;
  pub_chr[0] = 4;

  secp256k1_fe_normalize_var(&p.x);
  secp256k1_fe_normalize_var(&p.y);
  secp256k1_fe_get_b32(pub_chr +  1, &p.x);
  secp256k1_fe_get_b32(pub_chr + 33, &p.y);

  return 0;
}

static secp256k1_gej_t *batchpj;
static secp256k1_ge_t  *batchpa;
static secp256k1_fe_t  *batchaz;
static secp256k1_fe_t  *batchai;
static unsigned int     batchcap = 0;

int secp256k1_ec_pubkey_batch_init(unsigned int num) {
  if (num > batchcap) {
    free(batchpj); batchpj = NULL;
    free(batchpa); batchpa = NULL;
    free(batchaz); batchaz = NULL;
    free(batchai); batchai = NULL;
    batchpj = malloc(sizeof(secp256k1_gej_t)*num);
    batchpa = malloc(sizeof(secp256k1_ge_t)*num);
    batchaz = malloc(sizeof(secp256k1_fe_t)*num);
    batchai = malloc(sizeof(secp256k1_fe_t)*num);
    if (batchpj == NULL || batchpa == NULL || batchaz == NULL || batchai == NULL) {
      free(batchpj); batchpj = NULL;
      free(batchpa); batchpa = NULL;
      free(batchaz); batchaz = NULL;
      free(batchai); batchai = NULL;
      batchcap = 0;
      return 1;
    }
    batchcap = num;
  }
  return 0;
}

void secp256k1_ge_set_all_gej_static(int num, secp256k1_ge_t *batchpa, secp256k1_gej_t *batchpj) {
  size_t i;
  for (i = 0; i < num; i++) {
    batchaz[i] = batchpj[i].z;
  }

  secp256k1_fe_inv_all_var(num, batchai, batchaz);

  for (i = 0; i < num; i++) {
    secp256k1_ge_set_gej_zinv(&batchpa[i], &batchpj[i], &batchai[i]);
  }
}

// call secp256k1_ec_pubkey_batch_init first or you get segfaults
int secp256k1_ec_pubkey_batch_incr(unsigned int num, unsigned int skip, unsigned char (* restrict pub)[65], unsigned char (* restrict sec)[32], unsigned char start[32]) {
  // some of the values could be reused between calls, but dealing with the data
  // structures is a pain, and with a reasonable batch size, the perf difference
  // is tiny
  int i;

  unsigned char b32[32];

  secp256k1_scalar_t priv, incr_s;
  secp256k1_gej_t temp;
  secp256k1_ge_t incr_a;

  /* load staring private key */
  secp256k1_scalar_set_b32(&priv, start, NULL);

  /* fill first private */
  secp256k1_scalar_get_b32(sec[0], &priv);

  /* set up increments */
  secp256k1_scalar_set_int(&incr_s, skip);
  secp256k1_scalar_get_b32(b32, &incr_s);

#ifdef USE_BL_ARITHMETIC
  secp256k1_ecmult_gen_bl(&temp, b32);
  secp256k1_ecmult_gen_bl(&batchpj[0], start);
#else
  secp256k1_ecmult_gen2(&temp, b32);
  secp256k1_ecmult_gen2(&batchpj[0], start);
#endif

  /* get affine public point for incrementing */
  secp256k1_ge_set_gej_var(&incr_a, &temp);

  for (i = 1; i < num; ++i) {
    /* increment and write private key */
    secp256k1_scalar_add(&priv, &priv, &incr_s);
    secp256k1_scalar_get_b32(sec[i], &priv);

    /* increment public key */
    secp256k1_gej_add_ge_var(&batchpj[i], &batchpj[i-1], &incr_a, NULL);
  }

  /* convert all jacobian coordinates to affine */
  secp256k1_ge_set_all_gej_static(num, batchpa, batchpj);

  /* write out formatted public key */
  for (i = 0; i < num; ++i) {
    secp256k1_fe_normalize_var(&batchpa[i].x);
    secp256k1_fe_normalize_var(&batchpa[i].y);

    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &batchpa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &batchpa[i].y);
  }

  return 0;
}

// call secp256k1_ec_pubkey_batch_init first or you get segfaults
int secp256k1_ec_pubkey_batch_create(unsigned int num, unsigned char (* restrict pub)[65], unsigned char (* restrict sec)[32]) {
  int i;

  /* generate jacobian coordinates */
  for (i = 0; i < num; ++i) {
#ifdef USE_BL_ARITHMETIC
    secp256k1_ecmult_gen_bl(&batchpj[i], sec[i]);
#else
    secp256k1_ecmult_gen2(&batchpj[i], sec[i]);
#endif
  }

  /* convert all jacobian coordinates to affine */
  secp256k1_ge_set_all_gej_static(num, batchpa, batchpj);

  /* write out formatted public key */
  for (i = 0; i < num; ++i) {
    secp256k1_fe_normalize_var(&batchpa[i].x);
    secp256k1_fe_normalize_var(&batchpa[i].y);

    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &batchpa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &batchpa[i].y);
  }

  return 0;
}

int secp256k1_scalar_add_b32(void * out, void * a, void *b) {
  secp256k1_scalar_t tmp_a, tmp_b;

  secp256k1_scalar_set_b32(&tmp_a, a, NULL);
  secp256k1_scalar_set_b32(&tmp_b, b, NULL);
  secp256k1_scalar_add(&tmp_a, &tmp_a, &tmp_b);
  secp256k1_scalar_get_b32(out, &tmp_a);

  return 0;
}

inline static void _priv_add(unsigned char *priv, unsigned char add, int p) {
  priv[p] += add;
  if (priv[p] < add) {
    priv[--p] += 1;
    while (p) {
      if (priv[p] == 0) {
        priv[--p] += 1;
      } else {
        break;
      }
    }
  }
}

void priv_add_uint8(unsigned char *priv, unsigned char add) {
  _priv_add(priv, add, 31);
}

void priv_add_uint32(unsigned char *priv, unsigned int add) {
  uint64_t carry = (uint64_t)add;
  for (int i = 31; i >= 0 && carry; i--) {
    carry += priv[i];
    priv[i] = (unsigned char)(carry & 0xFF);
    carry >>= 8;
  }
}

typedef struct {
  secp256k1_gej_t pubj;
  secp256k1_ge_t  inc;
  secp256k1_gej_t incj;
  unsigned int n;
} pubkey_incr_t;

pubkey_incr_t pubkey_incr_ctx;

int secp256k1_ec_pubkey_incr_init(unsigned char *seckey, unsigned int add) {
  unsigned char incr_priv[32];
  memset(incr_priv, 0, sizeof(incr_priv));
  memset(&pubkey_incr_ctx, 0, sizeof(pubkey_incr_ctx));
  priv_add_uint32(incr_priv, add);

  pubkey_incr_ctx.n = add;

#ifdef USE_BL_ARITHMETIC
  secp256k1_ecmult_gen_bl(&pubkey_incr_ctx.pubj, seckey);
  secp256k1_ecmult_gen_bl(&pubkey_incr_ctx.incj, incr_priv);
#else
  secp256k1_ecmult_gen2(&pubkey_incr_ctx.pubj, seckey);
  secp256k1_ecmult_gen2(&pubkey_incr_ctx.incj, incr_priv);
#endif
  secp256k1_ge_set_gej(&pubkey_incr_ctx.inc, &pubkey_incr_ctx.incj);

  return 0;
}

int secp256k1_ec_pubkey_incr(unsigned char *pub_chr, int *pub_chr_sz, unsigned char *seckey) {
  secp256k1_ge_t p;

  priv_add_uint32(seckey, pubkey_incr_ctx.n);
#ifdef USE_BL_ARITHMETIC
  secp256k1_gej_add_ge_bl(&pubkey_incr_ctx.pubj, &pubkey_incr_ctx.pubj, &pubkey_incr_ctx.inc, NULL);
#else
  secp256k1_gej_add_ge_var(&pubkey_incr_ctx.pubj, &pubkey_incr_ctx.pubj, &pubkey_incr_ctx.inc, NULL);
#endif

  secp256k1_ge_set_gej(&p, &pubkey_incr_ctx.pubj);

  *pub_chr_sz = 65;
  pub_chr[0] = 4;

  secp256k1_fe_normalize_var(&p.x);
  secp256k1_fe_normalize_var(&p.y);
  secp256k1_fe_get_b32(pub_chr +  1, &p.x);
  secp256k1_fe_get_b32(pub_chr + 33, &p.y);

  return 0;
}

void secp256k1_ec_pubkey_batch_free(void) {
  free(batchpj);
  free(batchpa);
  free(batchaz);
  free(batchai);
  batchpj  = NULL;
  batchpa  = NULL;
  batchaz  = NULL;
  batchai  = NULL;
  batchcap = 0;
}

/* ---------- per-thread batch context ---------------------------------------- */

struct secp256k1_batch_s {
  secp256k1_gej_t *pj;
  secp256k1_ge_t  *pa;
  secp256k1_fe_t  *az;
  secp256k1_fe_t  *ai;
  unsigned int     cap;
};

int secp256k1_ec_pubkey_batch_alloc(secp256k1_batch_t **b, unsigned int num) {
  *b = malloc(sizeof(secp256k1_batch_t));
  if (!*b) return 1;
  (*b)->pj = malloc(sizeof(secp256k1_gej_t) * num);
  (*b)->pa = malloc(sizeof(secp256k1_ge_t)  * num);
  (*b)->az = malloc(sizeof(secp256k1_fe_t)  * num);
  (*b)->ai = malloc(sizeof(secp256k1_fe_t)  * num);
  if (!(*b)->pj || !(*b)->pa || !(*b)->az || !(*b)->ai) {
    free((*b)->pj); free((*b)->pa); free((*b)->az); free((*b)->ai);
    free(*b); *b = NULL;
    return 1;
  }
  (*b)->cap = num;
  return 0;
}

void secp256k1_ec_pubkey_batch_dealloc(secp256k1_batch_t *b) {
  if (!b) return;
  free(b->pj); free(b->pa); free(b->az); free(b->ai);
  free(b);
}

static void secp256k1_ge_set_all_gej_mt(unsigned int num, secp256k1_batch_t *b) {
  unsigned int i;
  for (i = 0; i < num; i++) b->az[i] = b->pj[i].z;
  secp256k1_fe_inv_all_var(num, b->ai, b->az);
  for (i = 0; i < num; i++)
    secp256k1_ge_set_gej_zinv(&b->pa[i], &b->pj[i], &b->ai[i]);
}

int secp256k1_ec_pubkey_batch_create_mt(secp256k1_batch_t *b, unsigned int num,
    unsigned char (*pub)[65], unsigned char (*sec)[32]) {
  unsigned int i;
  for (i = 0; i < num; i++) {
#ifdef USE_BL_ARITHMETIC
    secp256k1_ecmult_gen_bl(&b->pj[i], sec[i]);
#else
    secp256k1_ecmult_gen2(&b->pj[i], sec[i]);
#endif
  }
  secp256k1_ge_set_all_gej_mt(num, b);
  for (i = 0; i < num; i++) {
    secp256k1_fe_normalize_var(&b->pa[i].x);
    secp256k1_fe_normalize_var(&b->pa[i].y);
    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &b->pa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &b->pa[i].y);
  }
  return 0;
}

int secp256k1_ec_pubkey_batch_incr_mt(secp256k1_batch_t *b, unsigned int num,
    unsigned int skip, unsigned char (*pub)[65], unsigned char (*sec)[32],
    unsigned char start[32]) {
  unsigned int i;
  unsigned char b32[32];
  secp256k1_scalar_t priv, incr_s;
  secp256k1_gej_t temp;
  secp256k1_ge_t incr_a;

  secp256k1_scalar_set_b32(&priv, start, NULL);
  secp256k1_scalar_get_b32(sec[0], &priv);

  secp256k1_scalar_set_int(&incr_s, skip);
  secp256k1_scalar_get_b32(b32, &incr_s);

#ifdef USE_BL_ARITHMETIC
  secp256k1_ecmult_gen_bl(&temp,      b32);
  secp256k1_ecmult_gen_bl(&b->pj[0], start);
#else
  secp256k1_ecmult_gen2(&temp,      b32);
  secp256k1_ecmult_gen2(&b->pj[0], start);
#endif

  secp256k1_ge_set_gej_var(&incr_a, &temp);

  for (i = 1; i < num; i++) {
    secp256k1_scalar_add(&priv, &priv, &incr_s);
    secp256k1_scalar_get_b32(sec[i], &priv);
    secp256k1_gej_add_ge_var(&b->pj[i], &b->pj[i-1], &incr_a, NULL);
  }

  secp256k1_ge_set_all_gej_mt(num, b);

  for (i = 0; i < num; i++) {
    secp256k1_fe_normalize_var(&b->pa[i].x);
    secp256k1_fe_normalize_var(&b->pa[i].y);
    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &b->pa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &b->pa[i].y);
  }
  return 0;
}

/* ---------------------------------------------------------------------------- */

void secp256k1_ec_pubkey_precomp_table_free(void) {
  if (prec_mmapf.mem) {
    munmapf(&prec_mmapf);
    prec = NULL;
    prec_mmapf.mem = NULL;
  }
}

void * secp256k1_ec_priv_to_gej(unsigned char *priv) {
  secp256k1_gej_t *gej = malloc(sizeof(secp256k1_gej_t));
  if (gej == NULL) return NULL;
#ifdef USE_BL_ARITHMETIC
  secp256k1_ecmult_gen_bl(gej, priv);
#else
  secp256k1_ecmult_gen2(gej, priv);
#endif

  return gej;
}

int secp256k1_ec_pubkey_add_gej(unsigned char *pub_chr, int *pub_chr_sz, void *add) {
  secp256k1_ge_t  in;
  secp256k1_ge_t  p;

  secp256k1_gej_t out;

  secp256k1_eckey_pubkey_parse(&in, pub_chr, *pub_chr_sz);

#ifdef USE_BL_ARITHMETIC
  secp256k1_gej_add_ge_bl(&out, (secp256k1_gej_t *)add, &in, NULL);
#else
  secp256k1_gej_add_ge_var(&out, (secp256k1_gej_t *)add, &in, NULL);
#endif

  secp256k1_ge_set_gej(&p, &out);

  *pub_chr_sz = 65;
  pub_chr[0] = 4;

  secp256k1_fe_normalize_var(&p.x);
  secp256k1_fe_normalize_var(&p.y);
  secp256k1_fe_get_b32(pub_chr +  1, &p.x);
  secp256k1_fe_get_b32(pub_chr + 33, &p.y);

  return 0;
}

/*  vim: set ts=2 sw=2 et ai si: */
