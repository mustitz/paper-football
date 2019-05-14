bin_PROGRAMS = paper-football



if DEBUG_MODE
EXTRA_CFLAGS = -g3 -O0 -Wall -Werror
else
EXTRA_CFLAGS = -Ofast
endif



paper_football_CFLAGS = $(EXTRA_CFLAGS)
paper_football_SOURCES = main.c geometry.c parser.c utils.c
