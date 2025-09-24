// include/parser.h
#ifndef PARSER_H
#define PARSER_H

typedef struct {
    char **argv;        // Command arguments
    char *input_file;   // For input redirection: <
    char *output_file;  // For output redirection: >
    char *error_file;   // For error redirection: 2>
} Command;

typedef struct {
    Command *commands; // Dynamic array of Command structures
    int count; // Number of commands in the array 
} CommandList;

CommandList *parse_input(char *line); //primary parsing function that transforms user input into a machine-readable format.
void free_command_list(CommandList *cmdlist);  //implements a complete memory deallocation strategy for CommandList structures

#endif
