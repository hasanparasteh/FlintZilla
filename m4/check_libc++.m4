dnl Checks whether we need to pass -std=libc++ to CXXFLAGS. Sadly this is needed on OS X \
dnl which for some insane reason defaults to an ancient stdlibc++ :(
dnl To check for this, we try to use std::forward from <utility>

dnl Copyright (c) 2015-2016 Hasan Parasteh <hasanparasteh@gmail.com>

dnl Copying and distribution of this file, with or without modification, are
dnl permitted in any medium without royalty provided the copyright notice
dnl and this notice are preserved. This file is offered as-is, without any
dnl warranty.

AC_DEFUN([CHECK_LIBCXX], [

  AC_LANG_PUSH(C++)

  AC_MSG_CHECKING([for whether we need -stdlib=libc++])

  AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[
      #include <utility>
    ]], [[
      int x = 23;
      int y = std::forward<int>(x);
      return x == y ? 0 : 1;
    ]])
  ], [
    AC_MSG_RESULT([no])
  ], [
    CXXFLAGS="$CXXFLAGS -stdlib=libc++"
    LDFLAGS="$LDFLAGS -stdlib=libc++"

    AC_COMPILE_IFELSE([
      AC_LANG_PROGRAM([[
        #include <utility>
      ]], [[
        int x = 23;
        int y = std::forward<int>(x);
        return x == y ? 0 : 1;
      ]])
    ], [
      AC_MSG_RESULT([yes])
    ], [
      AC_MSG_FAILURE([std::forward in <utility> is not available or seems unusable.])
    ])
  ])

  AC_LANG_POP(C++)
])
