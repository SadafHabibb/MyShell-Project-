// src/parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

#define MAX_TOKENS 64
#define MAX_COMMANDS 32

void builtin_echo(char **argv) {
    int interpret_escapes = 0;
    int i = 1;

    if (argv[1] && strcmp(argv[1], "-e") == 0) {
        interpret_escapes = 1;
        i = 2;
    }

    for (; argv[i]; i++) {
        char *s = argv[i];
        if (interpret_escapes) {
            for (int j = 0; s[j]; j++) {
                if (s[j] == '\\') {
                    j++;
                    switch (s[j]) {
                        case 'n': putchar('\n'); break;
                        case 't': putchar('\t'); break;
                        case '\\': putchar('\\'); break;
                        case '"': putchar('"'); break;
                        case '\'': putchar('\''); break;
                        case '\0': j--; break;
                        default: putchar('\\'); putchar(s[j]); break;
                    }
                } else {
                    putchar(s[j]);
                }
            }
        } else {
            fputs(s, stdout);
        }
        if (argv[i + 1]) putchar(' ');
    }
    putchar('\n');
}

char *strip_quotes(char *token) {
    size_t len = strlen(token);
    if (len >= 2 &&
        ((token[0] == '"' && token[len-1] == '"') ||
         (token[0] == '\'' && token[len-1] == '\''))) {
        token[len-1] = '\0';
        token++;
    }
    return token;
}


// Parse user input into CommandList
CommandList *parse_input(char *line) {
    if (!line) return NULL;

    CommandList *cmdlist = malloc(sizeof(CommandList));
    if (!cmdlist) { perror("malloc"); return NULL; }

    cmdlist->count = 1;
    cmdlist->commands = malloc(MAX_COMMANDS * sizeof(Command));
    if (!cmdlist->commands) { perror("malloc"); free(cmdlist); return NULL; }

    // Initialize first command
    Command *cmd = &cmdlist->commands[0];
    cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
    for (int k = 0; k < MAX_TOKENS; k++) cmd->argv[k] = NULL;
    cmd->input_file = cmd->output_file = cmd->error_file = NULL;

    int i = 0;
    int current_command = 0;
    char *p = line;

    while (*p) {
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        // Handle pipe
        if (*p == '|') {
            if (i == 0) {
                fprintf(stderr, "Error: Empty command before or after pipe\n");
                free_command_list(cmdlist);
                return NULL;
            }
            cmd->argv[i] = NULL;
            current_command++;
            if (current_command >= MAX_COMMANDS) {
                fprintf(stderr, "Error: Too many commands in pipeline\n");
                free_command_list(cmdlist);
                return NULL;
            }
            cmd = &cmdlist->commands[current_command];
            cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
            for (int k = 0; k < MAX_TOKENS; k++) cmd->argv[k] = NULL;
            cmd->input_file = cmd->output_file = cmd->error_file = NULL;
            i = 0;
            p++;
            continue;
        }

        // Handle redirection
        char **target = NULL;
        if (*p == '<') { target = &cmd->input_file; p++; }
        else if (*p == '>') { target = &cmd->output_file; p++; }
        else if (*p == '2' && *(p+1) == '>') { target = &cmd->error_file; p+=2; }

        if (target) {
            while (*p && isspace(*p)) p++;
            if (!*p) {
                fprintf(stderr, "Error: Missing file for redirection\n");
                free_command_list(cmdlist);
                return NULL;
            }
            char buffer[1024];
            int j = 0;
            while (*p && !isspace(*p) && *p != '|' && j < 1023) buffer[j++] = *p++;
            buffer[j] = '\0';
            *target = strdup(buffer);
            continue;
        }

        // Handle normal arguments
        char buffer[1024];
        int j = 0;
        while (*p && !isspace(*p) && *p != '|') {
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                while (*p && *p != quote && j < 1023) buffer[j++] = *p++;
                if (*p == quote) p++;
            } else {
                buffer[j++] = *p++;
            }
        }
        buffer[j] = '\0';
        if (j == 0) continue;

        cmd->argv[i++] = strdup(buffer);
    }

    if (i == 0) {
        fprintf(stderr, "Error: Empty command at end of pipeline\n");
        free_command_list(cmdlist);
        return NULL;
    }
    cmd->argv[i] = NULL;
    cmdlist->count = current_command + 1;

    return cmdlist;
}

// Free memory
void free_command_list(CommandList *cmdlist) {
    if (!cmdlist) return;
    for (int c = 0; c < cmdlist->count; c++) {
        Command cmd = cmdlist->commands[c];
        if (cmd.argv) {
            for (int i = 0; i < MAX_TOKENS && cmd.argv[i]; i++)
                free(cmd.argv[i]);
            free(cmd.argv);
        }
        if (cmd.input_file) free(cmd.input_file);
        if (cmd.output_file) free(cmd.output_file);
        if (cmd.error_file) free(cmd.error_file);
    }
    free(cmdlist->commands);
    free(cmdlist);
}
