// include/parser.h
#ifndef PARSER_H
#define PARSER_H

typedef struct {
    char **argv;        //command arguments
    char *input_file;   //for input redirection: <
    char *output_file;  //for output redirection: >
    char *error_file;   //for error redirection: 2>
} Command;

typedef struct {
    Command *commands; //dynamic array of Command structures
    int count; //number of commands in the array 
} CommandList;

CommandList *parse_input(char *line); //primary parsing function that transforms user input into a machine-readable format.
void free_command_list(CommandList *cmdlist);  //implements a complete memory deallocation strategy for CommandList structures

//Add this declaration for builtin echo
void builtin_echo(char **argv);

#endif
