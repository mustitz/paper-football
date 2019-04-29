LOG_DRIVER = ./validation.sh

check_PROGRAMS = insider

if DEBUG_MODE
EXTRA_CFLAGS = -g3 -O0 -Wall -Werror
else
EXTRA_CFLAGS = -Ofast
endif

insider_CFLAGS = $(EXTRA_CFLAGS) -I../include
insider_SOURCES = insider.c

TESTS = run-insider

.PHONY : run-insider
