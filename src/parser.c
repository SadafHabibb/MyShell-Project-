// src/parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/parser.h"

#define MAX_TOKENS 64 //define max number of arguments per command
#define MAX_COMMANDS 32 //define max number of commands in a pipeline

// builtin echo command implementation
void builtin_echo(char **argv) {
    int interpret_escapes = 0;  //flag to interpret escape sequences like \n \t
    int i = 1;

    //check if -e flag is present
    if (argv[1] && strcmp(argv[1], "-e") == 0) {
        interpret_escapes = 1;
        i = 2;
    }

    //loop through all remaining arguments
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

//strip quotes from start and end of a token
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


//parse input line into command list
CommandList *parse_input(char *line) {
    if (!line) return NULL;

    CommandList *cmdlist = malloc(sizeof(CommandList)); //allocate command list
    if (!cmdlist) { perror("malloc"); return NULL; } // handle malloc failure

    cmdlist->count = 1; //at least one command by default
    cmdlist->commands = malloc(MAX_COMMANDS * sizeof(Command));
    if (!cmdlist->commands) { perror("malloc"); free(cmdlist); return NULL; }

    //Initialize first command
    Command *cmd = &cmdlist->commands[0];
    cmd->argv = malloc(MAX_TOKENS * sizeof(char *)); //allocate argv array
    for (int k = 0; k < MAX_TOKENS; k++) cmd->argv[k] = NULL; //initialize argv to null
    cmd->input_file = cmd->output_file = cmd->error_file = NULL; //initialize redirections

     //argument index for current command
    int i = 0;
    int current_command = 0;
    char *p = line;

    while (*p) {
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        //handle pipe
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
            //initialize new command
            cmd = &cmdlist->commands[current_command];
            cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
            for (int k = 0; k < MAX_TOKENS; k++) cmd->argv[k] = NULL;
            cmd->input_file = cmd->output_file = cmd->error_file = NULL;
            i = 0;
            p++;
            continue;
        }

        //handle redirection
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
            char buffer[1024];  //temp buffer for filename
            int j = 0;
            while (*p && !isspace(*p) && *p != '|' && j < 1023) buffer[j++] = *p++;
            buffer[j] = '\0';
            *target = strdup(buffer);
            continue;
        }

        //handle normal arguments
        char buffer[1024]; //buffer for normal argument
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

    if (i == 0) {  //empty command at end
        fprintf(stderr, "Error: Empty command at end of pipeline\n");
        free_command_list(cmdlist);
        return NULL;
    }
    cmd->argv[i] = NULL;  //null terminate last argv
    cmdlist->count = current_command + 1;

    return cmdlist;
}

//free memory allocated for command list
void free_command_list(CommandList *cmdlist) {
    if (!cmdlist) return;
    for (int c = 0; c < cmdlist->count; c++) {
        Command cmd = cmdlist->commands[c];
        if (cmd.argv) {
            for (int i = 0; i < MAX_TOKENS && cmd.argv[i]; i++)
                free(cmd.argv[i]);
            free(cmd.argv);
        }
        if (cmd.input_file) free(cmd.input_file); //free input redirection
        if (cmd.output_file) free(cmd.output_file); //free output redirection
        if (cmd.error_file) free(cmd.error_file); //free error redirection
    }
    free(cmdlist->commands); //free command array
    free(cmdlist);  //free command list struct
}