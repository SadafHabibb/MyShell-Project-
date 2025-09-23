// src/executor.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "executor.h"
#include <fcntl.h>

void execute_commands(CommandList *cmdlist) {
    for (int c = 0; c < cmdlist->count; c++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            exit(1);
        } else if (pid == 0) {
            // Child process
            Command *cmd = &cmdlist->commands[c];
            
            // Handle input redirection
            if (cmd->input_file != NULL) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd == -1) {
                    perror("Input file not found");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            
            execvp(cmdlist->commands[c].argv[0], cmdlist->commands[c].argv);
            perror("execvp failed");
            exit(1);
        } else {
            // Parent process waits
            int status;
            waitpid(pid, &status, 0);
        }
    }
}