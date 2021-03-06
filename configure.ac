# configure.ac for UniversalCodeGrep
#
# Copyright 2015-2016 Gary R. Van Sickle (grvs@users.sourceforge.net).
#
# This file is part of UniversalCodeGrep.
#
# UniversalCodeGrep is free software: you can redistribute it and/or modify it under the
# terms of version 3 of the GNU General Public License as published by the Free
# Software Foundation.
#
# UniversalCodeGrep is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.  See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# UniversalCodeGrep.  If not, see <http://www.gnu.org/licenses/>.

dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([UniversalCodeGrep],[0.3.0],[https://github.com/gvansickle/ucg/issues],[],[https://gvansickle.github.io/ucg/])

AC_CONFIG_SRCDIR([src/Globber.cpp])
AC_CONFIG_HEADERS([config.h])
AC_COPYRIGHT([Copyright (C) 2015-2016 Gary R. Van Sickle])

# Use the auxilliary build tools (e.g. install-sh, config.sub, etc.) in ./build-aux.
AC_CONFIG_AUX_DIR([build-aux])

# Additional Autoconf macros are in ./m4.
AC_CONFIG_MACRO_DIR([m4])

# Clear the "user defaults" for compiler optimization and debug flags.  This doesn't override any configure-time or make-time settings the
# builder may specifiy for these variables, it just makes the values empty if they're not specified at configure- or make-time.
# Otherwise, Autoconf's AC_PROG_CXX et al macros assume the builder wants to compile everything "-g -O2" by default, which overrides
# any AM_C*FLAGS.
# This otherwise-frowned-upon touching of these user variables is sanctioned by the Autoconf manual for this purpose.
# See the discussion of AC_PROG_CC in http://www.gnu.org/software/autoconf/manual/autoconf.html#C-Compiler, which reads:
# "If your package does not like this default, then it is acceptable to insert the line ': ${CFLAGS=""}' after AC_INIT and before AC_PROG_CC
# to select an empty default instead."
: ${CPPFLAGS=""}	# Use an empty default.
: ${CFLAGS=""}		# Don't default to "-g -O2".
: ${CXXFLAGS=""}	# Don't default to "-g -O2".

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
# Set up Automake.  Require at least automake 1.14
AM_INIT_AUTOMAKE([1.14
	foreign
	subdir-objects
	color-tests
	std-options
	-Wall
	-Werror
])

# To get access to get_current_dir_name() on Cygwin, we now need to specify _GNU_SOURCE.
# This will do that for us.
# @note Must be done before LT_INIT, or it will complain.
AC_USE_SYSTEM_EXTENSIONS

# Set up the maintainer compiler flags.
# Increase the default GCC warning level.
AC_SUBST([AM_CPPFLAGS], ["-Wall -Wextra"])
# By default, compile C and C++ with the maximum possible debugging info and maximum optimization.
# For debugging, do a "make CXXFLAGS=-O0".
AC_SUBST([AM_CFLAGS], ["-ggdb3 -O3"])
AC_SUBST([AM_CXXFLAGS], ["-ggdb3 -O3"])


###
### Checks for programs
###

## Checks for the programs needed to do a build.

AC_PROG_INSTALL
AC_PROG_CC_C99
# Per Automake 1.15 docs: "This is an obsolescent macro that checks that the C compiler supports the -c and -o options
#   together. Note that, since Automake 1.14, the AC_PROG_CC is rewritten to implement such checks itself, and thus the
#   explicit use of AM_PROG_CC_C_O should no longer be required."
#AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_CXXCPP # Needed for any C++ AC_PREPROC_IFELSE() checks below.
## Make sure we get a C++ compiler which supports C++11.
# Determine what switches we need to get at least some level of C++11 support.
# We do this here because this macro will replace $CXX with "$CXX -c++11switch", so we'll use this
# in the AC_LANG_PUSH([C++]) tests below.
# NOTE: The odd-looking ordering here (this before AC_PROG_CXXCPP) is required until the next version
# of this macro gets released.  See http://lists.gnu.org/archive/html/autoconf-archive-maintainers/2016-03/msg00034.html.
# @note 2016-10-8 Macro has been updated, trying to change the ordering.
AX_CXX_COMPILE_STDCXX_11([ext], [mandatory])
# @note The following line is to work around an arguable bug in Autoconf 1.12/Libtool.
# (q.v. http://lists.gnu.org/archive/html/automake/2012-05/msg00014.html).
# Probably remove this when/if that is fixed. 
#m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
# @note We still need AM_PROG_AR when we're using Libtool, or it will complain about a non-POSIX linker.
# See e.g. https://lists.gnu.org/archive/html/libtool/2012-11/msg00019.html.
AM_PROG_AR
# Shouldn't need ranlib since we're using libtool.
#AC_PROG_RANLIB

# Initialize Libtool.
LT_INIT([static disable-shared])
AC_SUBST([LIBTOOL_DEPS])

PKG_PROG_PKG_CONFIG([0.9.0])
AC_PROG_SED
# Find the sed option which turns on Extended Regular Expressions.
AC_CACHE_CHECK([for $SED option that supports extended regexes], [ac_cv_path_ESED],
	[ac_cv_path_ESED=''
	for EREoption in -r -E; do
	   	esedout=`echo "yeabcds" | $SED $EREoption 's/(abcd)//' 2> /dev/null`
	    test "x$esedout" = xyes \
	    && ac_cv_path_ESED="$SED $EREoption" ac_path_ESED_found=: && break
	done
	])
AS_IF([ test "x$ac_path_ESED_found" = "x:" ],
	[AC_SUBST([ESED], [$ac_cv_path_ESED])], # Note: AC_CACHE_CHECK handles the AC_MSG_RESULT() in the success case.
	[AC_MSG_ERROR([could not find $SED option that supports extended regexes])])

##
## Checks for programs needed to run the testsuite.
##

# The testsuite needs a way to invoke "script", to fool ucg into thinking it's outputting to a tty.
# Of course, Linux and *BSD (including OSX) "script"'s take different options and parameters, so we
# try to sort it out here.
AC_PATH_PROG([PROG_SCRIPT], [script], [:])
AC_MSG_CHECKING([$PROG_SCRIPT's command line type])
AS_IF([$PROG_SCRIPT -qfc "stty raw && echo hello" /dev/null > /dev/null 2>&1],
		[
		AC_SUBST([PROG_SCRIPT_TYPE], [linux])
		AC_SUBST([PROG_SCRIPT_PRE_TEXT], ['-qfc'])
		AC_SUBST([PROG_SCRIPT_POST_TEXT], ['/dev/null'])
		AC_MSG_RESULT([Linux style])
		],
      [$PROG_SCRIPT -q /dev/null $SHELL -c 'stty raw && echo "hello"' > /dev/null 2>&1],
		[
		AC_SUBST([PROG_SCRIPT_TYPE], [bsd])
		AC_SUBST([PROG_SCRIPT_PRE_TEXT], ['-q /dev/null'])
		AC_SUBST([PROG_SCRIPT_POST_TEXT], [])
		AC_MSG_RESULT([BSD style])
		],
      [AC_MSG_ERROR([could not determine how to invoke $PROG_SCRIPT])])

AC_PROG_MKDIR_P
AC_PROG_EGREP
AC_PROG_FGREP
AC_PROG_AWK
AC_PROG_LN_S
AS_IF([test "$LN_S" = 'ln -s'],
	[AC_SUBST([TEST_LN_S], [$LN_S])],
	[AC_SUBST([TEST_LN_S], [:])
	AC_MSG_WARN([couldn't find a real 'ln -s', will not be able to run all tests])])
	
# Try to find GNU grep.
AC_CACHE_CHECK([for GNU grep], [ac_cv_path_PROG_GNU_GREP],
[
	AC_PATH_PROGS_FEATURE_CHECK([PROG_GNU_GREP], [ggrep grep],
	[
		case `"$ac_path_PROG_GNU_GREP" --version 2>&1` in
		*GNU*)
		  ac_cv_path_PROG_GNU_GREP="$ac_path_PROG_GNU_GREP" ac_path_PROG_GNU_GREP_found=:;;
		*);;
		esac
	],
	[AC_MSG_WARN([could not find GNU grep])])
])
AC_SUBST([PROG_GNU_GREP], [$ac_path_PROG_GNU_GREP])

# Try to find a grep which supports -P (PCRE regexes).  Should be GNU grep.
AC_CACHE_CHECK([for a grep which supports PCREs], [ac_cv_path_PROG_GREP_P],
[
	AC_PATH_PROGS_FEATURE_CHECK([PROG_GREP_P], [ggrep grep],
	[
		grep_out=`echo abcfdef | $ac_path_PROG_GREP_P -Po 'a.*?f'`
		test "x$grep_out" = xabcf \
		&& ac_cv_path_PROG_GREP_P="$ac_path_PROG_GREP_P -P" ac_path_PROG_GREP_P_found=:
	],
	[AC_MSG_WARN([could not find a grep with PCRE support])])
])
AC_SUBST([PROG_GREP_P], [$ac_cv_path_PROG_GREP_P])

# See if we have a pcre2grep.
AC_PATH_PROG([PROG_PCRE2GREP], [pcre2grep], [])

###
### Checks for maintainer tools.
###

# Python, for generation of test suite.
AM_PATH_PYTHON([2.7],,[:])
AM_CONDITIONAL([HAVE_PYTHON], [test "x$PYTHON" != "x"])  #<< Automake conditional for skipping tests which require python.
AM_COND_IF([HAVE_PYTHON],
	[AX_PYTHON_MODULE([sqlite3],,[python])],
	[])
# @todo If we don't have the Python Sqlite3 library, we might as well not have python at all for
# our purposes here.  This is intended to scuttle the AM_MISSING_PROG([MAINTPYTHON]...) below, but
# it likely could be done better. 
AS_IF([test $HAVE_PYMOD_SQLITE3 = no], [PYTHON=:])

# Define a maintainer-only version of python, so a tarball builder doesn't need
# a real python runtime.
AM_MISSING_PROG([MAINTPYTHON], [$PYTHON])

# Autotest needs autom4te, or 'missing's stub for it post-distribution. 
AM_MISSING_PROG([AUTOM4TE], [autom4te])

# Add Valgrind support.
# @todo Note that this expands to GNU-make-only constructs, which fails on the BSDs.
#AX_VALGRIND_DFLT([memcheck], [off])
#AX_VALGRIND_DFLT([helgrind], [off])
#AX_VALGRIND_DFLT([drd], [off])
#AX_VALGRIND_DFLT([sgcheck], [off])
#AX_VALGRIND_CHECK

# Doxygen support.
DX_DOXYGEN_FEATURE(ON)
DX_DOT_FEATURE(ON)
DX_HTML_FEATURE(ON)
DX_CHM_FEATURE(OFF)
DX_CHI_FEATURE(OFF)
DX_MAN_FEATURE(OFF)
DX_RTF_FEATURE(OFF)
DX_XML_FEATURE(OFF)
DX_PDF_FEATURE(OFF)
DX_PS_FEATURE(OFF)
DX_INIT_DOXYGEN([UniversalCodeGrep])

###
### Checks for libraries
###

AC_LANG_PUSH([C++])

# We use libpcre or libpcre2 for regex matching.  As of this writing there are no C++11 implementations
# of <regex> in wide circulation (gcc 5.2, clang 3.7) which are robust enough (gcc SIGSEGVs on simple regexs)
# or even exist (clang has no Linux <regex> support) for production use.
PKG_CHECK_MODULES([PCRE], [libpcre >= 8.21],
	[
		# Found it.  Remember to add $PCRE_LIBS, $PCRE_CFLAGS, and $PCRE_CPPFLAGS to the appropriate automake vars.
		AC_SUBST([HAVE_LIBPCRE], [yes])
		AC_DEFINE([HAVE_LIBPCRE], [1], [Define if libpcre is available.])
	],
	[AC_SUBST([HAVE_LIBPCRE], [no])])

PKG_CHECK_MODULES([PCRE2], [libpcre2-8 >= 10.20],
	[
		# Found it.  Remember to add $PCRE2_LIBS, $PCRE2_CFLAGS, and $PCRE2_CPPFLAGS to the appropriate automake vars.
		AC_SUBST([HAVE_LIBPCRE2], [yes])
		AC_DEFINE([HAVE_LIBPCRE2], [1], [Define if libpcre2 is available.])
	],
	[AC_SUBST([HAVE_LIBPCRE2], [no])])

AC_LANG_POP([C++])


###
### Checks for header files.
###

AC_LANG_PUSH([C++])

# We don't have this header on MinGW.
AC_CHECK_HEADERS([pwd.h])

AC_LANG_POP([C++])

###
### Checks for types
###

AC_LANG_PUSH([C++])

AC_CHECK_TYPES([std::is_trivial<int>::value_type], [AC_DEFINE([HAVE_IS_TRIVIAL], [1],
	[Define if the std::is_trival<> template exists.])], [], [#include <type_traits>])
AC_CHECK_TYPES([std::is_trivially_copyable<int>::value_type], [AC_DEFINE([HAVE_IS_TRIVIALLY_COPYABLE], [1],
	[Define if the std::is_trivally_copyable<> template exists.])], [], [#include <type_traits>])

AC_LANG_POP([C++])

###
### Checks for structures
###

# N/A


###
### Checks for compiler characteristics.
###

# Check if the C compiler supports some flags we'd like to set.
# Append them to AM_CFLAGS if it does.
AC_LANG_PUSH([C])
AX_APPEND_COMPILE_FLAGS([-fno-omit-frame-pointer], [AM_CFLAGS])
AX_APPEND_COMPILE_FLAGS([-ftree-vectorize], [AM_CFLAGS])
AX_APPEND_COMPILE_FLAGS([-pthread], [AM_CFLAGS])
AC_LANG_POP([C])

AC_LANG_PUSH([C++])

# Check if the C++ compiler supports a "restrict" variant.
### @todo Works on Fedora 23/g++5.3.1 20160406 (Red Hat 5.3.1-6), doesn't work on Cygwin/g++ (GCC) 5.4.0.
#AC_C_RESTRICT

# Check if the C++ compiler supports some flags we'd like to set.
# Append them to AM_CXXFLAGS if it does.
AX_APPEND_COMPILE_FLAGS([-fno-omit-frame-pointer], [AM_CXXFLAGS])
#AX_APPEND_COMPILE_FLAGS([-save-temps], [AM_CXXFLAGS]) # -fverbose-asm
#AX_APPEND_COMPILE_FLAGS([-Wnoexcept], [AM_CXXFLAGS]) # Warn if something isn't marked noexcept when it could be. @note Doesn't seem to do anything.
AX_APPEND_COMPILE_FLAGS([-ftree-vectorize], [AM_CXXFLAGS]) # -fopt-info-optimized -fopt-info-vec-all
#AX_APPEND_COMPILE_FLAGS([-flto -ffat-lto-objects], [AM_CXXFLAGS])
AX_APPEND_COMPILE_FLAGS([-pthread], [AM_CXXFLAGS])
AX_APPEND_COMPILE_FLAGS([-Wformat -Wformat-security], [AM_CXXFLAGS])
AX_APPEND_COMPILE_FLAGS(['-Werror=format-security'], [AM_CXXFLAGS], [-Wformat -Wformat-security])

# Check for GCC-style built-ins and attributes we can use.
AX_C___ATTRIBUTE__	# Defines "HAVE___ATTRIBUTE__" if the compiler supports __attribute__(()) syntax.  The macro specifically checks for
					# the function attribute __attribute__((unused))
AX_GCC_FUNC_ATTRIBUTE([artificial])
AX_GCC_FUNC_ATTRIBUTE([const])
AX_GCC_FUNC_ATTRIBUTE([pure])
AX_GCC_FUNC_ATTRIBUTE([alloc_size])
AX_GCC_FUNC_ATTRIBUTE([malloc])
AX_GCC_BUILTIN(__builtin_unreachable)
AX_GCC_BUILTIN(__builtin_expect)
AX_GCC_BUILTIN(__builtin_popcount)
AX_GCC_BUILTIN(__builtin_bswap32)
AX_GCC_BUILTIN(__builtin_ctzll)

# Check for x86-64 ISA extensions we can use for function multiversioning.
GRVS_CHECK_COMPILE_FLAG([X86_64], [-msse2])
GRVS_CHECK_COMPILE_FLAG([X86_64], [-msse4.2])
GRVS_CHECK_COMPILE_FLAG([X86_64], [-mpopcnt], [-msse4.2])
GRVS_CHECK_COMPILE_FLAG([X86_64], [-mno-popcnt], [-msse4.2])
AM_CONDITIONAL([BUILD_X86_64_ISA_EXTENSIONS], [test "x$HAVE_X86_64_ISA_EXTENSIONS" != "x"])
AM_COND_IF([BUILD_X86_64_ISA_EXTENSIONS],
	[AC_MSG_NOTICE([Compiler supports x86-64 ISA extensions, will use them.])],
	[AC_MSG_NOTICE([Compiler does not support x86-64 ISA extensions, will not use them.])])


# Check if the implementation underlying C++11's std::thread<> is pthreads.  Note that we don't actually require pthreads,
# we require and use C++11's std::thread<>, whatever its implementation details.  However, if C++11 is using pthreads, we can
# optionally use some pthreads functionality, e.g. naming threads for diagnostic, logging, and other purposes.
# Macro docs: http://www.gnu.org/software/autoconf-archive/ax_pthread.html#ax_pthread
AX_PTHREAD
AS_IF([test "x$ax_pthread_ok" = xyes],
	[
		AC_MSG_NOTICE([found pthreads])
		AC_MSG_CHECKING([if the C++11 underlying thread implementation is pthreads])
		AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM([
        					#include <type_traits>
        					#include <thread>
        					#include <pthread.h> 					
        				],
                         [
                         	static_assert(std::is_same<std::thread::native_handle_type, pthread_t>::value, "C++11 underlying thread implementation is not pthreads");
                         ])],
        [
        	AC_DEFINE([CXX11_THREADS_ARE_PTHREADS], [1],
				[Define if the C++11 underlying thread implementation is pthreads])
        	AC_MSG_RESULT([yes])
        ],
        [
        	AC_MSG_RESULT([no])
        ])
		
	],
	[
		AC_MSG_NOTICE([no pthreads found])
	])

AC_LANG_POP([C++])


###
### Checks for library functions.
###

AC_LANG_PUSH([C++])

AC_FUNC_STRNLEN
AS_IF([test "x$ac_cv_func_strnlen_working" = "xyes"],
	[AC_DEFINE([HAVE_STRNLEN], [1], [Defined if strnlen() exists and is not broken.])])

# There's an issue with at least Cygwin's std lib and/or gcc itself which makes to_string() unavailable
# (see e.g. https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61580 (fixed on gcc trunk 2015-11-13),
# https://sourceware.org/ml/cygwin/2015-01/msg00251.html).  Check for existence here and we'll roll our own
# if necessary.
AC_CHECK_DECLS([std::to_string(int)],
				[AC_DEFINE([HAVE_TO_STRING], [1], [If defined, the std::to_string() function exists.])
				],
				[], [#include <string>])

# We need the argp library.
AC_SEARCH_LIBS([argp_parse], [argp], [], [AC_MSG_ERROR([cannot find the GNU Argp library or equivalent.])])

AC_CHECK_FUNCS([sched_setaffinity])

AC_CHECK_FUNCS([get_current_dir_name])

AC_CHECK_FUNCS([openat])

AC_CHECK_FUNCS([aligned_alloc posix_memalign])
AS_IF([test "x$ac_cv_func_aligned_alloc" = xno -a "x$ac_cv_func_posix_memalign" = xno],
	[AC_MSG_ERROR([cannot find an aligned memory allocator.])],
	[AC_MSG_NOTICE([found a usable aligned memory allocator.])])
	
AC_CHECK_FUNCS([posix_fadvise])

AC_MSG_CHECKING([if the GNU C library program_invocation{_short}_name strings are defined])
AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM([#include <errno.h>],
                         [program_invocation_short_name = "test";])],
        [
        	AC_DEFINE([HAVE_GNU_C_LIB_PROGRAM_INVOCATION_NAMES], [1],
				[Define if the GNU C library program_invocation{_short}_name strings are defined])
        	AC_MSG_RESULT([yes])
        ],
        [
        	AC_MSG_RESULT([no])
        ])

### Find a pthread_setname_np() we can use, and #define a macro M_pthread_setname_np() which will call it.
AC_MSG_CHECKING([how to set pthread names])
ptsetname=no
# For Linux.
AC_CHECK_DECLS([pthread_setname_np(pthread_t, const char *)],
				[AC_DEFINE([M_pthread_setname_np(name)], [pthread_setname_np(pthread_self(), (name))],
					[Macro for Linux to map to the lowest-common-denominator for this functionality, OSX])
				ptsetname=Linux
				],
				[], [#include <pthread.h>])
if test $ptsetname = no; then
# For FreeBSD.
AC_CHECK_DECLS([pthread_set_name_np(pthread_t, const char *)],
				[AC_DEFINE([M_pthread_setname_np(name)], [pthread_set_name_np(pthread_self(), (name))],
					[Macro for FreeBSD to map to the lowest-common-denominator for this functionality, OSX])
				 AC_DEFINE([INCLUDE_PTHREAD_NP], [1], [If defined, include the header <pthread_np.h> because we're on BSD])
				 ptsetname=BSD
				],
				[], [#include <pthread_np.h>])
fi
if test $ptsetname = no; then
# For Mac OSX.
AC_CHECK_DECLS([pthread_setname_np(const char *)],
				[AC_DEFINE([HAVE_NO_THREAD_LOCAL_SUPPORT], [1], [If defined, we have no C++11 thread_local support (clang on OSX)])
				 AC_DEFINE([M_pthread_setname_np(name)], [pthread_setname_np(name)],
					[Macro for OSX to map to itself])
				ptsetname=OSX
				], [], [#include <pthread.h>])
fi
AS_IF([test $ptsetname != no],
	[AC_DEFINE([HAVE_PTHREAD_SETNAME_SUPPORT], [1], [If defined, the macro M_pthread_setname_np() can be used to set the current thread's name])
	AC_MSG_RESULT([$ptsetname])],
	[AC_MSG_RESULT([none found])])

AC_LANG_POP([C++])


###
### Checks for system services
###

# N/A


###
### Initialize the test suite.
###
# Autoconf-supported test directory.
AC_CONFIG_TESTDIR([tests])
# The Autotest files to be generated by configure.
AC_CONFIG_FILES([tests/Makefile tests/atlocal])

### Valgrind support.
### @todo Put this in.
#AH_BOTTOM([#include <valgrind_support.h>])

###
### Create the Makefiles for the program.
###
AC_CONFIG_FILES([Makefile
	src/Makefile
	src/future/Makefile
	src/libext/Makefile
	third_party/Makefile])
AC_OUTPUT

# Print a configure summary.
AC_MSG_NOTICE([
*** Configuration Summary ***

  C compiler:                 $CC
  C++ compiler:               $CXX
  
  PCRE Info
  ---------
  HAVE_LIBPCRE                $HAVE_LIBPCRE
  HAVE_LIBPCRE2               $HAVE_LIBPCRE2
  
  libtool info
  ------------
  sys_lib_search_path_spec:   $sys_lib_search_path_spec
  sys_lib_dlsearch_path_spec: $sys_lib_dlsearch_path_spec
  runpath_var:                $runpath_var

])


# Make sure we found at least one of the PCRE libs.
AS_IF([test "x$HAVE_LIBPCRE" = x -a "x$HAVE_LIBPCRE2" = x],
	[AC_MSG_ERROR([unable to find a suitable pcre library.])],
	[AC_MSG_NOTICE([found at least one usable pcre library.])])

