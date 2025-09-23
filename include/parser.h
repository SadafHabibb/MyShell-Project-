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
    Command *commands;
    int count;
} CommandList;

CommandList *parse_input(char *line);
void free_command_list(CommandList *cmdlist);

#endif
