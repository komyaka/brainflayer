# ===========================================================================
#      https://www.gnu.org/software/autoconf-archive/ax_prog_cc_for_build.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_PROG_CC_FOR_BUILD
#
# DESCRIPTION
#
#   This macro searches for a C compiler that generates native executables,
#   that is a C compiler that surely is not a cross-compiler.  This can be
#   useful if you have to generate source code at compile-time like for
#   example GCC does.
#
#   The macro sets the CC_FOR_BUILD and CPP_FOR_BUILD macros to anything
#   needed to compile or link (CC_FOR_BUILD) and preprocess (CPP_FOR_BUILD).
#   The value of these variables can be overridden by the user by specifying
#   a compiler with an environment variable (like you do for standard CC).
#
#   It also sets BUILD_EXEEXT to the executable extension for the build
#   machine (empty string on Unix, .exe on Windows/MinGW).
#
# LICENSE
#
#   Copyright (c) 2008 Paolo Bonzini <bonzini@gnu.org>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.  This file is offered as-is, without any
#   warranty.

#serial 14

AU_ALIAS([AC_PROG_CC_FOR_BUILD], [AX_PROG_CC_FOR_BUILD])
AC_DEFUN([AX_PROG_CC_FOR_BUILD], [dnl
AC_REQUIRE([AC_PROG_CC])dnl
AC_REQUIRE([AC_PROG_CPP])dnl
AC_REQUIRE([AC_CANONICAL_HOST])dnl

dnl Use the standard macros, but make them use other variable names
pushdef([ac_cv_prog_CPP], ac_cv_build_prog_CPP)dnl
pushdef([ac_cv_prog_gcc], ac_cv_build_prog_gcc)dnl
pushdef([ac_cv_prog_cc_works], ac_cv_build_prog_cc_works)dnl
pushdef([ac_cv_prog_cc_cross], ac_cv_build_prog_cc_cross)dnl
pushdef([ac_cv_prog_cc_g], ac_cv_build_prog_cc_g)dnl
pushdef([ac_cv_prog_cc_c89], ac_cv_build_prog_cc_c89)dnl
pushdef([CC], CC_FOR_BUILD)dnl
pushdef([CPP], CPP_FOR_BUILD)dnl
pushdef([CFLAGS], CFLAGS_FOR_BUILD)dnl
pushdef([CPPFLAGS], CPPFLAGS_FOR_BUILD)dnl
pushdef([LDFLAGS], LDFLAGS_FOR_BUILD)dnl
pushdef([host], build)dnl
pushdef([host_alias], build_alias)dnl
pushdef([host_cpu], build_cpu)dnl
pushdef([host_vendor], build_vendor)dnl
pushdef([host_os], build_os)dnl
pushdef([ac_cv_host], ac_cv_build)dnl
pushdef([ac_cv_host_alias], ac_cv_build_alias)dnl

save_cross_compiling=$cross_compiling
save_ac_tool_prefix=$ac_tool_prefix
cross_compiling=no
ac_tool_prefix=

AC_PROG_CC
AC_PROG_CPP

cross_compiling=$save_cross_compiling
ac_tool_prefix=$save_ac_tool_prefix

popdef([ac_cv_host_alias])dnl
popdef([ac_cv_host])dnl
popdef([host_os])dnl
popdef([host_vendor])dnl
popdef([host_cpu])dnl
popdef([host_alias])dnl
popdef([host])dnl
popdef([LDFLAGS])dnl
popdef([CPPFLAGS])dnl
popdef([CFLAGS])dnl
popdef([CPP])dnl
popdef([CC])dnl
popdef([ac_cv_prog_cc_c89])dnl
popdef([ac_cv_prog_cc_g])dnl
popdef([ac_cv_prog_cc_cross])dnl
popdef([ac_cv_prog_cc_works])dnl
popdef([ac_cv_prog_gcc])dnl
popdef([ac_cv_prog_CPP])dnl

AC_SUBST(CC_FOR_BUILD)
AC_SUBST(CFLAGS_FOR_BUILD)
AC_SUBST(CPPFLAGS_FOR_BUILD)
AC_SUBST(LDFLAGS_FOR_BUILD)

dnl Determine BUILD_EXEEXT (extension for executables on the build machine)
if test "$build" = "$host" ; then
  BUILD_EXEEXT="$EXEEXT"
else
  BUILD_EXEEXT=""
fi
AC_SUBST(BUILD_EXEEXT)
])
