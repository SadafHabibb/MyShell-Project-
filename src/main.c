#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "executor.h"

int main() {
    char *line = NULL;
    size_t len = 0;

    while (1) {
        // Print clean shell prompt
        printf("$ ");
        fflush(stdout);

        // Read a line from stdin
        if (getline(&line, &len, stdin) == -1) {
            printf("\n");  // handle Ctrl+D
            break;
        }

        // Remove trailing newline
        line[strcspn(line, "\n")] = 0;

        // Skip empty lines
        if (line[0] == '\0') continue;

        // Built-in exit
        if (strcmp(line, "exit") == 0) {
            break;  // exit the shell loop
        }

        // Parse input line into commands
        CommandList *cmdlist = parse_input(line);
        if (cmdlist == NULL) continue;  // empty or invalid input

        // Execute parsed commands
        execute_commands(cmdlist);

        // Free allocated memory for commands
        free_command_list(cmdlist);
    }

    free(line);
    return 0;
}
