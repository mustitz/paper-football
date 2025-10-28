AC_DEFUN([MU_LEAKS], [
    case "$host_os" in
      darwin*)
        leaks_withval=auto

        AC_ARG_WITH([leaks],
          AS_HELP_STRING([--with-leaks], [Use leaks to run testsuite]),
          [leaks_withval=$withval]
        )

        AS_CASE(
          [$leaks_withval],

          [no],
          [
            leaks_withval=
          ],

          [yes],
          [
            AC_CHECK_PROG([have_leaks], [leaks], [yes], [no])
            AS_IF(
              [test "x$have_leaks" == "xyes"],
              [leaks_withval=leaks],
              [AC_MSG_ERROR([leaks not found in PATH.])])
          ],

          [auto],
          [
            AC_CHECK_PROG([have_leaks], [leaks], [yes], [no])
            AS_IF(
              [test "x$have_leaks" == "xyes"],
              [leaks_withval=leaks],
              [leaks_withval=])
          ],

          [
            AC_CHECK_FILE([$leaks_withval], [have_leaks=yes], [have_leaks=no])
            AS_IF(
              [test "x$have_leaks" != "xyes"],
              [
                leaks_withval=
                AC_MSG_ERROR([leaks not found in PATH.])
              ])
          ]
        )

        AC_SUBST([LEAKS], [$leaks_withval])
        ;;
      *)
        AC_SUBST([LEAKS], [])
        ;;
    esac
])
