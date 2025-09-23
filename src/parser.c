// src/parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

#define MAX_TOKENS 64  // Maximum number of command arguments we can handle

CommandList *parse_input(char *line) {
    // Allocate memory for a list of commands
    CommandList *cmdlist = malloc(sizeof(CommandList));
    if (!cmdlist) {
        perror("malloc failed");
        exit(1);
    }

    // Only support 1 command (no pipes yet)
    cmdlist->count = 1;
    cmdlist->commands = malloc(sizeof(Command));
    if (!cmdlist->commands) {
        perror("malloc failed");
        exit(1);
    }

    // Pointer to the first command
    Command *cmd = &cmdlist->commands[0];

    // Allocate space for command arguments
    cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
    if (!cmd->argv) {
        perror("malloc failed");
        exit(1);
    }

    cmd->input_file = NULL;   // For < redirection
    cmd->output_file = NULL;  // For > redirection
    cmd->error_file = NULL;   // For 2> redirection

    int i = 0;  // Index for arguments
    char *token = strtok(line, " \t\n");  // Split input by space, tab, newline

    while (token != NULL && i < MAX_TOKENS - 1) {
        if (strcmp(token, "<") == 0) {
            // Input redirection
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing input file after '<'\n");
                break;
            }
            cmd->input_file = strdup(token);
        } else if (strcmp(token, ">") == 0) {
            // Output redirection
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing output file after '>'\n");
                break;
            }
            cmd->output_file = strdup(token);
        } else if (strcmp(token, "2>") == 0) {
            // Error redirection
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing error file after '2>'\n");
                break;
            }
            cmd->error_file = strdup(token);
        } else {
            // Normal command argument
            cmd->argv[i++] = strdup(token);
        }
        token = strtok(NULL, " \t\n");
    }

    cmd->argv[i] = NULL;  // Null-terminate argv
    return cmdlist;
}

// Free allocated memory
void free_command_list(CommandList *cmdlist) {
    if (!cmdlist) return;

    for (int c = 0; c < cmdlist->count; c++) {
        Command cmd = cmdlist->commands[c];
        for (int i = 0; cmd.argv[i] != NULL; i++) {
            free(cmd.argv[i]);
        }
        free(cmd.argv);

        if (cmd.input_file) free(cmd.input_file);
        if (cmd.output_file) free(cmd.output_file);
        if (cmd.error_file) free(cmd.error_file);
    }

    free(cmdlist->commands);
    free(cmdlist);
}
