// include/executor.h
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h" //include the parser header to access CommandList and Command structure definitions

// Execute parsed command(s)
void execute_commands(CommandList *cmdlist); //serves as the primary execution engine for the shell, taking parsed
// command information from the parser and executing each command in separate child processes.

#endif
