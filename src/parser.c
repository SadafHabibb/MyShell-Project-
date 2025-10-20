// src/parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glob.h>
#include "parser.h"

#define MAX_TOKENS 64  //maximum number of command arguments we can handle
#define MAX_COMMANDS 32  //maximum number of commands in a pipeline


void builtin_echo(char **argv) {
    int interpret_escapes = 0;
    int i = 1;

    // check for -e
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

//helper function to remove surrounding single or double quotes from a token
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

//takes a line of text input from the user and breaks it into a structured format
CommandList *parse_input(char *line) {
    //allocate memory for a list of commands that will support multiple commands for pipes
    //this structure now handles both single commands and complex pipelines
    CommandList *cmdlist = malloc(sizeof(CommandList));
    if (!cmdlist) {
        perror("malloc failed"); //print an error if memory allocation fails
        exit(1);                 //exit the program because we cannot continue
    }

    //initialize command count to 1 (will be incremented when pipes are found)
    //this maintains compatibility with existing single-command functionality
    //while allowing expansion for pipe implementation
    cmdlist->count = 1;
    
    //allocate initial memory for command array with room for multiple commands
    //start with space for MAX_COMMANDS to handle complex pipelines
    cmdlist->commands = malloc(MAX_COMMANDS * sizeof(Command));
    if (!cmdlist->commands) {
        perror("malloc failed"); //print an error if memory allocation fails
        exit(1);
    }

    //initialize the first command structure
    //this will be the only command for non-piped input, or the first command in a pipeline
    Command *cmd = &cmdlist->commands[0];
    
    //allocate space for command arguments in the first command
    cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
    if (!cmd->argv) {
        perror("malloc failed");
        exit(1);
    }

    //initialize redirection fields for the first command
    //these may be set during parsing if redirection operators are found
    cmd->input_file = NULL;   //for < redirection (typically on first command)
    cmd->output_file = NULL;  //for > redirection (typically on last command)
    cmd->error_file = NULL;   //for 2> redirection (can be on any command)

    //initialize argument index for building argv array of current command
    //this tracks where to place the next argument in the current command
    int i = 0;
    
    //track which command we're currently parsing (starts with command 0)
    //this will be incremented each time a pipe symbol is encountered
    int current_command = 0;
    
    //pointer to traverse the input line manually for quote-aware tokenization
    char *p = line;

    //main parsing loop: process each token until end of input or token limit reached
    while (*p && i < MAX_TOKENS - 1) {
        //skip leading whitespace
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        //check for pipe
        if (*p == '|') {
            if (i == 0) {
                fprintf(stderr, "Error: Empty command before pipe\n");
                free_command_list(cmdlist);
                return NULL;
            }
            cmd->argv[i] = NULL;
            cmdlist->count++;
            if (cmdlist->count > MAX_COMMANDS) {
                fprintf(stderr, "Error: Too many commands in pipeline (maximum %d)\n", MAX_COMMANDS);
                free_command_list(cmdlist);
                return NULL;
            }
            current_command++;
            cmd = &cmdlist->commands[current_command];
            cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
            if (!cmd->argv) { perror("malloc failed"); exit(1); }
            cmd->input_file = cmd->output_file = cmd->error_file = NULL;
            i = 0;
            p++; // skip the pipe
            continue;
        }

        //handle input/output/error redirection
        if (strncmp(p, "<", 1) == 0) {
            p++;
            while (*p && isspace(*p)) p++;
            if (!*p) { fprintf(stderr, "Error: Missing input file after '<'\n"); free_command_list(cmdlist); return NULL; }
            char buffer[1024];
            int j = 0;
            while (*p && !isspace(*p)) buffer[j++] = *p++;
            buffer[j] = '\0';
            cmd->input_file = strdup(buffer);
            continue;
        } else if (strncmp(p, ">", 1) == 0) {
            p++;
            while (*p && isspace(*p)) p++;
            if (!*p) { fprintf(stderr, "Error: Missing output file after '>'\n"); free_command_list(cmdlist); return NULL; }
            char buffer[1024];
            int j = 0;
            while (*p && !isspace(*p)) buffer[j++] = *p++;
            buffer[j] = '\0';
            cmd->output_file = strdup(buffer);
            continue;
        } else if (strncmp(p, "2>", 2) == 0) {
            p += 2;
            while (*p && isspace(*p)) p++;
            if (!*p) { fprintf(stderr, "Error: Missing error file after '2>'\n"); free_command_list(cmdlist); return NULL; }
            char buffer[1024];
            int j = 0;
            while (*p && !isspace(*p)) buffer[j++] = *p++;
            buffer[j] = '\0';
            cmd->error_file = strdup(buffer);
            continue;
        }

        //parse a normal argument, handling quotes and concatenation
        char buffer[1024];
        int j = 0;
        while (*p && !isspace(*p) && *p != '|') {
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                while (*p && *p != quote) {
                    buffer[j++] = *p++;
                }
                if (*p == quote) p++;
            } else {
                buffer[j++] = *p++;
            }
        }
        buffer[j] = '\0';
        if (j == 0) continue;

        //handle globbing
        glob_t results;
        int glob_flags = 0;
        if (glob(buffer, glob_flags, NULL, &results) == 0) {
            for (size_t k = 0; k < results.gl_pathc && i < MAX_TOKENS - 1; k++)
                cmd->argv[i++] = strdup(results.gl_pathv[k]);
            globfree(&results);
        } else {
            cmd->argv[i++] = strdup(buffer);
        }
    }

    //null-terminate last command's argv
    if (i == 0) {
        fprintf(stderr, "Error: Empty command at end of pipeline\n");
        free_command_list(cmdlist);
        return NULL;
    }
    cmd->argv[i] = NULL;

    return cmdlist;
}

//free allocated memory - this function remains the same as it already handles multiple commands
//the existing implementation correctly iterates through cmdlist->count commands
//and frees all allocated memory for each command including redirection filenames
void free_command_list(CommandList *cmdlist) {
    if (!cmdlist) return; // Nothing to free

    //iterate through all commands in the list (now supports multiple commands)
    for (int c = 0; c < cmdlist->count; c++) {
        Command cmd = cmdlist->commands[c];
        
        //free all argument strings for this command
        for (int i = 0; cmd.argv[i] != NULL; i++) {
            free(cmd.argv[i]);
        }

        //free the arguments array itself
        free(cmd.argv);

        //free redirection filenames if they were allocated
        if (cmd.input_file) free(cmd.input_file);
        if (cmd.output_file) free(cmd.output_file);
        if (cmd.error_file) free(cmd.error_file);
    }

    //free the commands array and the CommandList structure
    free(cmdlist->commands);

    //finally, free the CommandList struct
    free(cmdlist);
}
