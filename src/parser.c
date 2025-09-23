// src/parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

#define MAX_TOKENS 64 // Maximum number of command arguments we can handle

CommandList *parse_input(char *line) {
    // Allocate memory for a list of commands
    CommandList *cmdlist = malloc(sizeof(CommandList));

    // For now, we only support 1 command (no pipes yet)
    cmdlist->count = 1;

    // Allocate memory for the command(s)
    cmdlist->commands = malloc(sizeof(Command));

    // Shortcut pointer to first command
    Command *cmd = &cmdlist->commands[0];

    // Allocate space for command arguments (tokens)
    cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
    cmd->output_file = NULL;  // For > redirection
    cmd->error_file = NULL;   // For 2> redirection


    int i = 0;  // Index for storing arguments
    char *token = strtok(line, " \t\n");   // Split input by spaces, tabs, or newlines
    while (token != NULL && i < MAX_TOKENS - 1) {
        if (strcmp(token, ">") == 0) {
            // stdout redirection
            token = strtok(NULL, " \t\n");
            if (token == NULL) {
                fprintf(stderr, "Error: Missing output file after '>'\n");
                break;
            }
            cmd->output_file = strdup(token);
        } else if (strcmp(token, "2>") == 0) {
            // stderr redirection
            token = strtok(NULL, " \t\n");
            if (token == NULL) {
                fprintf(stderr, "Error: Missing error file after '2>'\n");
                break;
            }
            cmd->error_file = strdup(token);
        } else {
            // normal argument
            cmd->argv[i++] = strdup(token);
        }
        token = strtok(NULL, " \t\n");
    }
    cmd->argv[i] = NULL; // null terminate

    return cmdlist;
}

void free_command_list(CommandList *cmdlist) {
    if (!cmdlist) return;
    for (int c = 0; c < cmdlist->count; c++) {
        Command cmd = cmdlist->commands[c];
        for (int i = 0; cmd.argv[i] != NULL; i++) {
            free(cmd.argv[i]);
        }
        free(cmd.argv);
        if (cmd.output_file) free(cmd.output_file);
        if (cmd.error_file) free(cmd.error_file);
    }
    free(cmdlist->commands);
    free(cmdlist);
}
