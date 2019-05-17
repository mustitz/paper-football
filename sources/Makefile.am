bin_PROGRAMS = paper-football
BUILT_SOURCES = hashes.h



if DEBUG_MODE
EXTRA_CFLAGS = -g3 -O0 -Wall -Werror
else
EXTRA_CFLAGS = -Ofast
endif



paper_football_CFLAGS = $(EXTRA_CFLAGS)
paper_football_SOURCES = main.c game.c random-ai.c parser.c utils.c calc-hash.awk

hashes.h: calc-hash.awk random-ai.c
	sha512sum random-ai.c | awk -f calc-hash.awk > hashes.h
