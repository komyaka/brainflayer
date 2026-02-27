dnl Macros for libsecp256k1 configure.ac
dnl
dnl These macros are used by secp256k1/configure.ac to detect
dnl platform capabilities and set the appropriate build flags.

dnl SECP_64BIT_ASM_CHECK
dnl Sets has_64bit_asm=yes if x86_64 inline assembly is available.
AC_DEFUN([SECP_64BIT_ASM_CHECK],[
  AC_MSG_CHECKING([for x86_64 assembly availability])
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
    #include <stdint.h>
    int main() {
      uint64_t a = 11, tmp;
      __asm__ __volatile__("movq $0x100000003, %1; mulq %%rax; andq %1, %1;" : "+a"(a), "=d"(tmp) :: "cc");
      return 0;
    }
  ]])],
  [has_64bit_asm=yes],
  [has_64bit_asm=no])
  AC_MSG_RESULT([$has_64bit_asm])
])

dnl SECP_INT128_CHECK
dnl Sets has_int128=yes if the compiler supports unsigned __int128.
AC_DEFUN([SECP_INT128_CHECK],[
  AC_MSG_CHECKING([for __int128 support])
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
    typedef unsigned __int128 uint128_t;
    int main() {
      uint128_t a = 0;
      (void)a;
      return 0;
    }
  ]])],
  [has_int128=yes],
  [has_int128=no])
  AC_MSG_RESULT([$has_int128])
])

dnl SECP_GMP_CHECK
dnl Sets has_gmp=yes and adjusts LIBS if libgmp is available.
AC_DEFUN([SECP_GMP_CHECK],[
  if test x"$has_gmp" != x"yes"; then
    CPPFLAGS_TEMP="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GMP_CPPFLAGS"
    LIBS_TEMP="$LIBS"
    LIBS="$LIBS $GMP_LIBS -lgmp"
    AC_MSG_CHECKING([for libgmp])
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
      #include <gmp.h>
      int main() {
        mpz_t integ;
        mpz_init(integ);
        mpz_clear(integ);
        return 0;
      }
    ]])],
    [has_gmp=yes],
    [has_gmp=no])
    CPPFLAGS="$CPPFLAGS_TEMP"
    LIBS="$LIBS_TEMP"
    if test x"$has_gmp" = x"yes"; then
      LIBS="$LIBS $GMP_LIBS -lgmp"
    fi
    AC_MSG_RESULT([$has_gmp])
  fi
])

dnl SECP_OPENSSL_CHECK
dnl Sets has_openssl_ec=yes and configures SSL_CFLAGS/CRYPTO_CFLAGS/CRYPTO_LIBS
dnl if OpenSSL EC functions are available.
AC_DEFUN([SECP_OPENSSL_CHECK],[
  has_openssl_ec=no
  if test x"$use_pkgconfig" = x"yes"; then
    PKG_CHECK_MODULES([SSL],[openssl],[
      PKG_CHECK_MODULES([CRYPTO],[libcrypto],[
        AC_CHECK_LIB([crypto],[EC_KEY_new_by_curve_name],[
          has_openssl_ec=yes
        ],[
          has_openssl_ec=no
        ],[$CRYPTO_LIBS])
      ],[has_openssl_ec=no])
    ],[has_openssl_ec=no])
  else
    AC_CHECK_LIB([crypto],[EC_KEY_new_by_curve_name],[
      CRYPTO_LIBS=-lcrypto
      has_openssl_ec=yes
    ],[has_openssl_ec=no])
  fi
])
