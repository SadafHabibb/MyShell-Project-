// src/executor.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "executor.h"

void execute_commands(CommandList *cmdlist) { // Execute all commands in a CommandList with comprehensive I/O redirection support
    for (int c = 0; c < cmdlist->count; c++) { // Iterate through each command in the CommandList structure
        pid_t pid = fork(); // Create a new child process using fork() system call
        if (pid < 0) { // fork() failed - print system error message and terminate the shell
            perror("Fork failed");
            exit(1);
        } else if (pid == 0) { //Child process code path - this code executes in the newly created process
            // Child process
            Command *cmd = &cmdlist->commands[c]; // Get direct reference to the current command structure for easier access

            // --- Input redirection '<' ---
            if (cmd->input_file != NULL) {
                int fd = open(cmd->input_file, O_RDONLY);  // Open the specified input file in read-only mode
                if (fd == -1) { // File opening failed - print error message with system details
                    perror("Input file not found");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO); // Redirect stdin (file descriptor 0) to point to the input file
                close(fd); // Close the original file descriptor as it's no longer needed
            }

            // --- Output redirection '>' ---
            if (cmd->output_file != NULL) {   // Open the output file with write permissions and specific creation/truncation behavior
                int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                // Check if file creation/opening was successful
                if (fd == -1) {
                    perror("Output file"); // File operation failed - provide error details to help diagnose the problem
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO); // dup2() makes STDOUT_FILENO become a duplicate of the file descriptor
                close(fd);
            }

            // --- Error redirection '2>' ---
            if (cmd->error_file != NULL) { 
                // Open the error file with write permissions and creation/truncation behavior
                int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("Error file"); // Error file operation failed - report the problem before exiting
                    exit(1);
                }
                dup2(fd, STDERR_FILENO); // dup2() makes STDERR_FILENO become a duplicate of the file descriptor
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
