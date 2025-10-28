AC_INIT([paper-football], [0.1], [mustitz@gmail.com])
${CFLAGS=""}

AM_INIT_AUTOMAKE(dist-bzip2 subdir-objects)
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST



AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug], [enable debugging, default: no]),
    [case "${enableval}" in
        yes) debug=true ;;
        no)  debug=false ;;
        *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
    esac],
[debug=false])

AM_CONDITIONAL(DEBUG_MODE, test x"$debug" = x"true")

AC_ARG_ENABLE([mcts-logs],
    AS_HELP_STRING([--enable-mcts-logs], [enable MCTS logging, default: no]),
    [case "${enableval}" in
        yes) mcts_logs=true ;;
        no)  mcts_logs=false ;;
        *)   AC_MSG_ERROR([bad value ${enableval} for --enable-mcts-logs]) ;;
    esac],
[mcts_logs=false])

AS_IF([test x"$mcts_logs" = x"true"],
    [AC_DEFINE([ENABLE_MCTS_LOGS], [1], [Enable MCTS logging])],
    [AC_DEFINE([ENABLE_MCTS_LOGS], [0], [Enable MCTS logging])])

MU_VALGRIND
MU_LEAKS



AC_PROG_CC_C99
AM_SILENT_RULES([yes])
AC_SEARCH_LIBS([sqrt, log], [m])



AC_CONFIG_HEADERS([include/config.h])
AC_CONFIG_FILES([Makefile sources/Makefile include/Makefile validation/Makefile])
AC_CONFIG_FILES([validation/validation.sh], [chmod +x validation/validation.sh])



AC_OUTPUT
