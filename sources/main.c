#include "paper-football.h"

#include <stdio.h>

int process_cmd(const char * const line)
{
    return 1;
}

int main()
{
    char * line = 0;
    size_t len = 0;
    for (;; ) {
        const ssize_t has_read = getline(&line, &len, stdin);
        if (has_read == -1) {
            break;
        }

        const int is_quit = process_cmd(line);
        if (is_quit) {
            break;
        }
    }

    if (line) {
        free(line);
    }

    return 0;
}
