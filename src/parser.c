// parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

// Maximum number of arguments a single command can have.
// We set this limit to avoid allocating too much memory unnecessarily.
#define MAX_TOKENS 64

/*
 * Function: parse_input
 * Takes a line of text input from the user and breaks it into a structured format
 */
CommandList *parse_input(char *line) {
    // Allocate memory for the CommandList structure
    // This will hold all commands that we parse from the input.
    CommandList *cmdlist = malloc(sizeof(CommandList));
    if (!cmdlist) {
        perror("malloc failed"); // Print an error if memory allocation fails
        exit(1);                 // Exit the program because we cannot continue
    }

    // Set the number of commands to 1 (single command support)
    cmdlist->count = 1;

    // Allocate memory for the commands array inside CommandList
    cmdlist->commands = malloc(sizeof(Command));
    if (!cmdlist->commands) {
        perror("malloc failed"); // Print an error if memory allocation fails
        exit(1);
    }

    // Pointer to the first (and only) command
    Command *cmd = &cmdlist->commands[0];

    // Allocate memory for storing the command arguments (argv)
    // Each argument will be stored as a string (char pointer)
    cmd->argv = malloc(MAX_TOKENS * sizeof(char *));
    if (!cmd->argv) {
        perror("malloc failed");
        exit(1);
    }

    // Initialize input/output/error redirection fields to NULL
    // This means that by default, no redirection is set
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->error_file = NULL;

    // Index to track how many arguments we have added to argv
    int i = 0;

    // Use strtok to split the input line into tokens separated by space, tab, or newline
    char *token = strtok(line, " \t\n");

    // Loop through all tokens until we reach the end or exceed MAX_TOKENS
    while (token != NULL && i < MAX_TOKENS - 1) { // Leave space for NULL at the end
        if (strcmp(token, "<") == 0) {
            // If token is "<", the next token is the input file
            token = strtok(NULL, " \t\n");
            if (!token) { // If no file is provided, print an error
                fprintf(stderr, "Error: Missing input file after '<'\n");
                break; // Stop parsing
            }
            cmd->input_file = strdup(token); // Copy the filename
        } else if (strcmp(token, ">") == 0) {
            // If token is ">", the next token is the output file
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing output file after '>'\n");
                break;
            }
            cmd->output_file = strdup(token); // Copy the filename
        } else if (strcmp(token, "2>") == 0) {
            // If token is "2>", the next token is the error file
            token = strtok(NULL, " \t\n");
            if (!token) {
                fprintf(stderr, "Error: Missing error file after '2>'\n");
                break;
            }
            cmd->error_file = strdup(token); // Copy the filename
        } else {
            // Otherwise, this is a normal command argument (like "ls" or "-l")
            cmd->argv[i++] = strdup(token); // Copy the argument string
        }

        // Move to the next token in the line
        token = strtok(NULL, " \t\n");
    }

    // Always null-terminate argv so it can be used with exec family functions
    cmd->argv[i] = NULL;

    // Return the fully parsed CommandList
    return cmdlist;
}

/*
 * Function: free_command_list
 * Frees all memory allocated for a CommandList.
 * This prevents memory leaks in the program.
 */
void free_command_list(CommandList *cmdlist) {
    if (!cmdlist) return; // Nothing to free

    // Loop through each command (currently only 1 command)
    for (int c = 0; c < cmdlist->count; c++) {
        Command cmd = cmdlist->commands[c];

        // Free each argument string individually
        for (int i = 0; cmd.argv[i] != NULL; i++) {
            free(cmd.argv[i]);
        }

        // Free the arguments array itself
        free(cmd.argv);

        // Free any input/output/error filenames if they exist
        if (cmd.input_file) free(cmd.input_file);
        if (cmd.output_file) free(cmd.output_file);
        if (cmd.error_file) free(cmd.error_file);
    }

    // Free the array of commands
    free(cmdlist->commands);

    // Finally, free the CommandList struct
    free(cmdlist);
}
