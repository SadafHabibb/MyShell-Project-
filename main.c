#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "executor.h"

int main() {
    char *line = NULL;
    size_t len = 0;

    while (1) {
        printf("myshell> ");
        fflush(stdout);

        if (getline(&line, &len, stdin) == -1) {
            printf("\n");
            break; // EOF (Ctrl+D)
        }

        // Remove trailing newline
        line[strcspn(line, "\n")] = 0;

        // Skip empty lines
        if (line[0] == '\0') continue;

        // Built-in exit
        if (strcmp(line, "exit") == 0) {
            printf("Bye!\n");
            break;
        }

        // Parse and execute
        CommandList *cmdlist = parse_input(line);
        execute_commands(cmdlist);
        free_command_list(cmdlist);
    }

    free(line);
    return 0;
}
