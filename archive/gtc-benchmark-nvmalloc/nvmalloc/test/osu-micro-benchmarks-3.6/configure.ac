#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.59])
AC_INIT([OSU-Micro-Benchmarks], [3.6], [mvapich-discuss@cse.ohio-state.edu])
AC_CONFIG_SRCDIR([osu_get_latency.c])

AM_INIT_AUTOMAKE([foreign])

AC_ARG_ENABLE([cuda],
              [AS_HELP_STRING([--enable-cuda],
                              [Enable CUDA benchmarks])
              ],
              [],
              [enable_cuda=no])

AC_ARG_WITH([cuda],
            [AS_HELP_STRING([--with-cuda=@<:@CUDA installation path@:>@],
                            [Provide path to CUDA installation])
            ],
            [AS_CASE([$with_cuda],
                     [yes|no], [],
                     [CPPFLAGS="-I$with_cuda/include $CPPFLAGS"
                      LDFLAGS="-L$with_cuda/lib64 -Wl,-rpath=$with_cuda/lib64 -L$with_cuda/lib -Wl,-rpath=$with_cuda/lib $LDFLAGS"])
            ])

AC_ARG_WITH([cuda-include],
            [AS_HELP_STRING([--with-cuda-include=@<:@CUDA include path@>:@],
                            [Provide path to CUDA include files])
            ],
            [AS_CASE([$with_cuda_include],
                     [yes|no], [],
                     [CPPFLAGS="-I$with_cuda_include $CPPFLAGS"])
            ])

AC_ARG_WITH([cuda-libpath],
            [AS_HELP_STRING([--with-cuda-libpath=@<:@CUDA library path@>:@],
                            [Provide path to CUDA library files])
            ],
            [AS_CASE([$with_cuda_libpath],
                     [yes|no], [],
                     [LDFLAGS="-L$with_cuda_libpath -Wl,-rpath=$with_cuda_libpath $LDFLAGS"])
            ])

# Checks for programs.
AC_PROG_CC([mpicc])

# Checks for libraries.
AC_SEARCH_LIBS([sqrt], [m])
AC_SEARCH_LIBS([pthread_create], [pthread])

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([getpagesize gettimeofday memset sqrt])
AC_CHECK_FUNC([MPI_Accumulate], [mpi2_library=true])

AS_IF([test "x$enable_cuda" = xyes], [
       AC_SEARCH_LIBS([cuPointerGetAttribute], [cuda], [],
                      [AC_MSG_ERROR([cannot link with -lcuda])])
       AC_SEARCH_LIBS([cudaFree], [cudart], [],
                      [AC_MSG_ERROR([cannot link with -lcudart])])
       AC_CHECK_HEADERS([cuda.h], [],
                        [AC_MSG_ERROR([cannot include cuda.h])])
       AC_DEFINE([_ENABLE_CUDA_], [1], [Enable CUDA])
       ])

AM_CONDITIONAL([MPI2_LIBRARY], [test x$mpi2_library = xtrue])
AM_CONDITIONAL([CUDA], [test x$enable_cuda = xyes])

AC_DEFINE([FIELD_WIDTH], [18], [Width of field used to report numbers])
AC_DEFINE([FLOAT_PRECISION], [2], [Precision of reported numbers])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
