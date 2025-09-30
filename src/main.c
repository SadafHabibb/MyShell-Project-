#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "executor.h"

int main() {
    char line[1024];

    while (1) {
        // Print clean shell prompt
        printf("$ ");
        fflush(stdout);

        // Read a line from stdin
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");  // handle Ctrl+D
            break;
        }

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        // Skip empty lines
        if (line[0] == '\0') continue;

        // Built-in exit
        if (strcmp(line, "exit") == 0) {
            break;
        }

        // Parse input line into commands
        CommandList *cmdlist = parse_input(line);
        if (cmdlist == NULL) continue;

        // Execute parsed commands
        execute_commands(cmdlist);

        // Free allocated memory
        free_command_list(cmdlist);
    }

    return 0;
}