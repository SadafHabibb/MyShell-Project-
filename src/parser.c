// src/parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

#define MAX_TOKENS 64

CommandList *parse_input(char *line) {
    CommandList *cmdlist = malloc(sizeof(CommandList));
    cmdlist->count = 1; // for now, only one command
    cmdlist->commands = malloc(sizeof(Command));
    
    Command *cmd = &cmdlist->commands[0];
    cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
    cmd->input_file = NULL; // Initialize to NULL
    
    int i = 0;
    char *token = strtok(line, " \t\n");
    while (token != NULL && i < MAX_TOKENS - 1) {
        // Check for input redirection
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t\n");
            if (token == NULL) {
                printf("Error: Input file not specified after '<'\n");
                free_command_list(cmdlist);
                return NULL;
            }
            cmd->input_file = strdup(token); // Store the input filename
        } else {
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
        
        // Free input file if it exists
        if (cmd.input_file) {
            free(cmd.input_file);
        }
    }
    free(cmdlist->commands);
    free(cmdlist);
}
