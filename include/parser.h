// include/parser.h
#ifndef PARSER_H
#define PARSER_H

typedef struct {
    char **argv;  // Null-terminated array of arguments for execvp
    char *input_file;
} Command;

typedef struct {
    Command *commands;
    int count;
} CommandList;

// Parse a raw input line into CommandList
CommandList *parse_input(char *line);

// Free memory allocated by parser
void free_command_list(CommandList *cmdlist);

#endif
