// src/executor.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "executor.h"

void execute_commands(CommandList *cmdlist) {
    for (int c = 0; c < cmdlist->count; c++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            exit(1);
        } else if (pid == 0) {
            // Child process
            Command *cmd = &cmdlist->commands[c];

            // --- Input redirection '<' ---
            if (cmd->input_file != NULL) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd == -1) {
                    perror("Input file not found");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            // --- Output redirection '>' ---
            if (cmd->output_file != NULL) {
                int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("Output file");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            // --- Error redirection '2>' ---
            if (cmd->error_file != NULL) {
                int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("Error file");
                    exit(1);
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            // --- Execute command with arguments ---
            execvp(cmd->argv[0], cmd->argv);
            perror("execvp failed"); // if execvp fails
            exit(1);
        } else {
            // Parent process waits for child
            int status;
            waitpid(pid, &status, 0);
        }
    }
}
