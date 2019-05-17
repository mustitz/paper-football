LOG_DRIVER = ./validation.sh

check_PROGRAMS = insider

if DEBUG_MODE
EXTRA_CFLAGS = -g3 -O0 -Wall -Werror
else
EXTRA_CFLAGS = -Ofast
endif

insider_CFLAGS = -DMAKE_CHECK $(EXTRA_CFLAGS) -I../include
insider_SOURCES = insider.c ../sources/utils.c ../sources/parser.c ../sources/game.c ../sources/random-ai.c

TESTS = run-insider

.PHONY : run-insider
