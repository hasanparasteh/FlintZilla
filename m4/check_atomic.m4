# Some versions of gcc/libstdc++ require linking with -latomic if
# using the C++ atomic library.

# Copyright (c) 2015-2016 Hasan Parasteh <hasanparasteh@gmail.com>

# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice
# and this notice are preserved. This file is offered as-is, without any
# warranty.

m4_define([_CHECK_ATOMIC_testbody], [[
  #include <atomic>
  #include <cstdint>

  int main() {
    std::atomic<int64_t> a{};

    int64_t v = 5;
    int64_t r = a.fetch_add(v);
    return static_cast<int>(r);
  }
]])

AC_DEFUN([CHECK_ATOMIC], [

  AC_LANG_PUSH(C++)

  AC_MSG_CHECKING([whether std::atomic can be used without link library])

  AC_LINK_IFELSE([AC_LANG_SOURCE([_CHECK_ATOMIC_testbody])],[
      AC_MSG_RESULT([yes])
    ],[
      AC_MSG_RESULT([no])
      LIBS="$LIBS -latomic"
      AC_MSG_CHECKING([whether std::atomic needs -latomic])
      AC_LINK_IFELSE([AC_LANG_SOURCE([_CHECK_ATOMIC_testbody])],[
          AC_MSG_RESULT([yes])
        ],[
          AC_MSG_RESULT([no])
          AC_MSG_FAILURE([cannot figure out how to use std::atomic])
        ])
    ])

  AC_LANG_POP
])
