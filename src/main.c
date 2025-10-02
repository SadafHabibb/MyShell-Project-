#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "executor.h"

int main() {
    char line[1024];

    while (1) {
        //print clean shell prompt
        printf("$ ");
        fflush(stdout);

        //read a line from stdin
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");  //handle Ctrl+D
            break;
        }

        //remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        //skip empty lines
        if (line[0] == '\0') continue;

        //built-in exit
        if (strcmp(line, "exit") == 0) {
            break;
        }

        //parse input line into commands
        CommandList *cmdlist = parse_input(line);
        if (cmdlist == NULL) continue;

        //execute parsed commands
        execute_commands(cmdlist);

        //free allocated memory
        free_command_list(cmdlist);
    }

    return 0;
}