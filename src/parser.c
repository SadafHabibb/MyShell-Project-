
// src/parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

#define MAX_TOKENS 64  // Maximum number of command arguments we can handle
#define MAX_COMMANDS 32  // Maximum number of commands in a pipeline

/*
 * Function: parse_input
 * Takes a line of text input from the user and breaks it into a structured format
 */
CommandList *parse_input(char *line) {
    // Allocate memory for a list of commands that will support multiple commands for pipes
    // This structure now handles both single commands and complex pipelines
    CommandList *cmdlist = malloc(sizeof(CommandList));
    if (!cmdlist) {
        perror("malloc failed"); // Print an error if memory allocation fails
        exit(1);                 // Exit the program because we cannot continue
    }

    // Initialize command count to 1 (will be incremented when pipes are found)
    // This maintains compatibility with existing single-command functionality
    // while allowing expansion for pipe implementation
    cmdlist->count = 1;
    
    // Allocate initial memory for command array with room for multiple commands
    // Start with space for MAX_COMMANDS to handle complex pipelines
    cmdlist->commands = malloc(MAX_COMMANDS * sizeof(Command));
    if (!cmdlist->commands) {
        perror("malloc failed"); // Print an error if memory allocation fails
        exit(1);
    }

    // Initialize the first command structure
    // This will be the only command for non-piped input, or the first command in a pipeline
    Command *cmd = &cmdlist->commands[0];
    
    // Allocate space for command arguments in the first command
    cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
    if (!cmd->argv) {
        perror("malloc failed");
        exit(1);
    }

    // Initialize redirection fields for the first command
    // These may be set during parsing if redirection operators are found
    cmd->input_file = NULL;   // For < redirection (typically on first command)
    cmd->output_file = NULL;  // For > redirection (typically on last command)
    cmd->error_file = NULL;   // For 2> redirection (can be on any command)

    // Initialize argument index for building argv array of current command
    // This tracks where to place the next argument in the current command
    int i = 0;
    
    // Track which command we're currently parsing (starts with command 0)
    // This will be incremented each time a pipe symbol is encountered
    int current_command = 0;
    
    // Begin tokenization of the input string
    // strtok() splits the input by whitespace delimiters
    char *token = strtok(line, " \t\n");

    // Main parsing loop: process each token until end of input or token limit reached
    // This loop now handles both regular arguments/redirections AND pipe symbols
    while (token != NULL && i < MAX_TOKENS - 1) {
        
        // Check if current token is a pipe symbol indicating command separation
        // Pipes connect stdout of previous command to stdin of next command
        if (strcmp(token, "|") == 0) {
            // Pipe symbol found - need to start a new command in the pipeline
            
            // First, validate that the current command has at least one argument
            // An empty command before a pipe is a syntax error
            if (i == 0) {
                fprintf(stderr, "Error: Empty command before pipe\n");
                free_command_list(cmdlist);
                return NULL;
            }
            
            // Null-terminate the current command's argv array
            // This completes the argument list for the current command
            cmd->argv[i] = NULL;
            
            // Increment the total command count to include the new command after the pipe
            cmdlist->count++;
            
            // Check if we've exceeded the maximum number of commands in a pipeline
            if (cmdlist->count > MAX_COMMANDS) {
                fprintf(stderr, "Error: Too many commands in pipeline (maximum %d)\n", MAX_COMMANDS);
                free_command_list(cmdlist);
                return NULL;
            }
            
            // Move to the next command in the pipeline
            current_command++;
            cmd = &cmdlist->commands[current_command];
            
            // Allocate memory for the new command's argument vector
            cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
            if (!cmd->argv) {
                perror("malloc failed");
                exit(1);
            }
            
            // Initialize redirection fields for the new command
            // Middle commands in a pipeline typically don't have file redirection
            // but we initialize them for safety and potential future use
            cmd->input_file = NULL;
            cmd->output_file = NULL;
            cmd->error_file = NULL;
            
            // Reset argument index for the new command
            i = 0;
            
            // Get next token to start parsing the new command
            token = strtok(NULL, " \t\n");
            
            // Check if there's a command after the pipe symbol
            if (token == NULL) {
                fprintf(stderr, "Error: Missing command after pipe\n");
                free_command_list(cmdlist);
                return NULL;
            }
            
            // Continue to process the first token of the new command
            // (don't skip to next iteration, process this token normally)
        }
        
        // Handle input redirection operator
        // Input redirection is typically only valid on the first command in a pipeline
        if (strcmp(token, "<") == 0) {
            // Get the filename for input redirection
            token = strtok(NULL, " \t\n");
            if (!token) { // If no file is provided, print an error
                fprintf(stderr, "Error: Missing input file after '<'\n");
                free_command_list(cmdlist);
                return NULL;
            }
            
            // Warn if input redirection is used on non-first command (may be valid in complex cases)
            if (current_command > 0) {
                fprintf(stderr, "Warning: Input redirection on command %d in pipeline\n", current_command + 1);
            }
            
            cmd->input_file = strdup(token);
            
        } else if (strcmp(token, ">") == 0) {
            // Handle output redirection operator
            // Output redirection is typically only valid on the last command in a pipeline
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing output file after '>'\n");
                free_command_list(cmdlist);
                return NULL;
            }
            cmd->output_file = strdup(token);
            
        } else if (strcmp(token, "2>") == 0) {
            // Handle error redirection operator
            // Error redirection can be valid on any command in a pipeline
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing error file after '2>'\n");
                free_command_list(cmdlist);
                return NULL;
            }
            cmd->error_file = strdup(token);
            
        } else {
            // Regular command argument - add to current command's argv array
            // This includes the command name (first argument) and all command options
            cmd->argv[i++] = strdup(token);
        }
        
        // Get next token for continued parsing
        token = strtok(NULL, " \t\n");
    }
    
    // After parsing all tokens, validate that we have at least one argument in the final command
    // This catches cases like "command1 | " where there's a trailing pipe with no command
    if (i == 0) {
        fprintf(stderr, "Error: Empty command at end of pipeline\n");
        free_command_list(cmdlist);
        return NULL;
    }

    // Null-terminate the final command's argv array
    // This completes the argument list for the last command in the pipeline
    cmd->argv[i] = NULL;
    
    // Return the completed CommandList with all parsed commands and their redirection settings
    return cmdlist;
}

// Free allocated memory - this function remains the same as it already handles multiple commands
// The existing implementation correctly iterates through cmdlist->count commands
// and frees all allocated memory for each command including redirection filenames
void free_command_list(CommandList *cmdlist) {
    if (!cmdlist) return; // Nothing to free

    // Iterate through all commands in the list (now supports multiple commands)
    for (int c = 0; c < cmdlist->count; c++) {
        Command cmd = cmdlist->commands[c];
        
        // Free all argument strings for this command
        for (int i = 0; cmd.argv[i] != NULL; i++) {
            free(cmd.argv[i]);
        }

        // Free the arguments array itself
        free(cmd.argv);

        // Free redirection filenames if they were allocated
        if (cmd.input_file) free(cmd.input_file);
        if (cmd.output_file) free(cmd.output_file);
        if (cmd.error_file) free(cmd.error_file);
    }

    // Free the commands array and the CommandList structure
    free(cmdlist->commands);

    // Finally, free the CommandList struct
    free(cmdlist);
}