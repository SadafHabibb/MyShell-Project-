
// src/parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

#define MAX_TOKENS 64  //maximum number of command arguments we can handle
#define MAX_COMMANDS 32  //maximum number of commands in a pipeline

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
    
    //begin tokenization of the input string
    //strtok() splits the input by whitespace delimiters
    char *token = strtok(line, " \t\n");

    //main parsing loop: process each token until end of input or token limit reached
    //this loop now handles both regular arguments/redirections AND pipe symbols
    while (token != NULL && i < MAX_TOKENS - 1) {
        
        //check if current token is a pipe symbol indicating command separation
        //pipes connect stdout of previous command to stdin of next command
        if (strcmp(token, "|") == 0) {
            //pipe symbol found - need to start a new command in the pipeline
            
            //first, validate that the current command has at least one argument
            //an empty command before a pipe is a syntax error
            if (i == 0) {
                fprintf(stderr, "Error: Empty command before pipe\n");
                free_command_list(cmdlist);
                return NULL;
            }
            
            //null-terminate the current command's argv array
            //this completes the argument list for the current command
            cmd->argv[i] = NULL;
            
            //increment the total command count to include the new command after the pipe
            cmdlist->count++;
            
            //check if we've exceeded the maximum number of commands in a pipeline
            if (cmdlist->count > MAX_COMMANDS) {
                fprintf(stderr, "Error: Too many commands in pipeline (maximum %d)\n", MAX_COMMANDS);
                free_command_list(cmdlist);
                return NULL;
            }
            
            //move to the next command in the pipeline
            current_command++;
            cmd = &cmdlist->commands[current_command];
            
            //allocate memory for the new command's argument vector
            cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
            if (!cmd->argv) {
                perror("malloc failed");
                exit(1);
            }
            
            //initialize redirection fields for the new command
            //middle commands in a pipeline typically don't have file redirection
            //but we initialize them for safety and potential future use
            cmd->input_file = NULL;
            cmd->output_file = NULL;
            cmd->error_file = NULL;
            
            //reset argument index for the new command
            i = 0;
            
            //get next token to start parsing the new command
            token = strtok(NULL, " \t\n");
            
            //check if there's a command after the pipe symbol
            if (token == NULL) {
                fprintf(stderr, "Error: Missing command after pipe\n");
                free_command_list(cmdlist);
                return NULL;
            }
            
            //continue to process the first token of the new command
            //(don't skip to next iteration, process this token normally)
        }
        
        //handle input redirection operator
        //input redirection is typically only valid on the first command in a pipeline
        if (strcmp(token, "<") == 0) {
            //get the filename for input redirection
            token = strtok(NULL, " \t\n");
            if (!token) { //if no file is provided, print an error
                fprintf(stderr, "Error: Missing input file after '<'\n");
                free_command_list(cmdlist);
                return NULL;
            }
            
            //warn if input redirection is used on non-first command (may be valid in complex cases)
            if (current_command > 0) {
                fprintf(stderr, "Warning: Input redirection on command %d in pipeline\n", current_command + 1);
            }
            
            cmd->input_file = strdup(token);
            
        } else if (strcmp(token, ">") == 0) {
            //handle output redirection operator
            //output redirection is typically only valid on the last command in a pipeline
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing output file after '>'\n");
                free_command_list(cmdlist);
                return NULL;
            }
            cmd->output_file = strdup(token);
            
        } else if (strcmp(token, "2>") == 0) {
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing error file after '2>'\n");
                free_command_list(cmdlist);
                return NULL;
            }
            //determine which command to attach error redirection to
            //if we are in the middle of a command (i > 0), attach to current command
            //otherwise, attach to the previous command if it exists
            cmd->error_file = strdup(token);
        } else {
            //regular command argument - add to current command's argv array
            //this includes the command name (first argument) and all command options
            cmd->argv[i++] = strdup(strip_quotes(token));
        }
        
        //get next token for continued parsing
        token = strtok(NULL, " \t\n");
    }
    
    //after parsing all tokens, validate that we have at least one argument in the final command
    //this catches cases like "command1 | " where there's a trailing pipe with no command
    if (i == 0) {
        fprintf(stderr, "Error: Empty command at end of pipeline\n");
        free_command_list(cmdlist);
        return NULL;
    }

    //null-terminate the final command's argv array
    //this completes the argument list for the last command in the pipeline
    cmd->argv[i] = NULL;
    
    //return the completed CommandList with all parsed commands and their redirection settings
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