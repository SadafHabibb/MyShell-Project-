// src/executor.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "executor.h"


// This function  handles both single commands and pipes with multiple commands
void execute_commands(CommandList *cmdlist) {
    // Check if this is a single command (no pipes), use original simple execution
    // This maintains compatibility with existing functionality and provides optimized
    // execution path for single commands without pipe overhead
    if (cmdlist->count == 1) {
        // Single command execution path - original logic with all redirection support
        // This handles commands without pipes using the proven existing implementation
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            exit(1);
        } else if (pid == 0) {
            // Child process for single command
            Command *cmd = &cmdlist->commands[0];

            // Apply all redirection using existing proven code
            // Input redirection: redirect stdin from file if specified
            if (cmd->input_file != NULL) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd == -1) {
                    perror("Input file not found");
                    exit(1);}
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            // Output redirection: redirect stdout to file if specified
            if (cmd->output_file != NULL) {
                int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("Output file");
                    exit(1);}
                dup2(fd, STDOUT_FILENO);
                close(fd);
            } 

            // Error redirection: redirect stderr to file if specified
            if (cmd->error_file != NULL) {
                int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("Error file");
                    exit(1);}
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            // Execute the single command
            execvp(cmd->argv[0], cmd->argv);
            perror("execvp failed");
            exit(1);
        } else {
            // Parent waits for single command to complete
            int status;
            waitpid(pid, &status, 0);
        }
        return;
    }

    // Pipeline execution path - multiple commands connected by pipes
    // This section handles complex pipelines where stdout of each command connects
    // to stdin of the next command, with optional redirection on first/last commands
    
    // Array to store all pipe file descriptors for the pipeline
    // Each pipe connects two adjacent commands in the pipeline
    // We need (count-1) pipes to connect count commands
    int pipes[cmdlist->count - 1][2];
    
    // Array to store process IDs of all child processes in the pipeline
    // We need to track all processes to wait for them properly
    pid_t pids[cmdlist->count];

    // Create all pipes needed for the pipeline before forking any processes
    // This ensures all pipe file descriptors are available when setting up connections
    for (int i = 0; i < cmdlist->count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Pipe creation failed");
            exit(1);
        }
    }

    // Fork and set up each command in the pipeline
    // Each command is set up with appropriate input/output connections and redirection
    for (int c = 0; c < cmdlist->count; c++) {
        pids[c] = fork();
        
        if (pids[c] < 0) {
            perror("Fork failed in pipeline");
            exit(1);
        } else if (pids[c] == 0) {
            // Child process code for command c in the pipeline
            Command *cmd = &cmdlist->commands[c];

            // Set up input for this command in the pipeline
            // First command: may read from input file or stdin
            // Middle/last commands: read from previous pipe
            if (cmd->input_file) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd == -1) { perror("Input file"); exit(1); }
                dup2(fd, STDIN_FILENO); close(fd);
            } else if (c > 0) {
                dup2(pipes[c-1][0], STDIN_FILENO);
            }

            // Set up output for this command in the pipeline
            // Last command: may write to output file or stdout
            // First/middle commands: write to next pipe
            if (cmd->output_file) {
                int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) { perror("Output file"); exit(1); }
                dup2(fd, STDOUT_FILENO); close(fd);
            } else if (c < cmdlist->count - 1) {
                dup2(pipes[c][1], STDOUT_FILENO);
            }

            // Handle error redirection for any command in the pipeline
            // Error redirection can be applied to any command independently of pipes
            // This allows capturing errors from specific commands in the pipeline
            if (cmd->error_file) {
                int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) { perror("Error file"); exit(1); }
                dup2(fd, STDERR_FILENO); close(fd);
            }


            // Close all pipe file descriptors in the child process
            // After dup2 operations, the original pipe file descriptors are not needed
            // Closing them prevents file descriptor leaks and allows proper pipe closure
            // Each child must close all pipe descriptors to avoid blocking other processes
            for (int i = 0; i < cmdlist->count - 1; i++) {
                close(pipes[i][0]);  // Close read end of each pipe
                close(pipes[i][1]);  // Close write end of each pipe
            }

            // Execute the command for this stage of the pipeline
            // At this point, stdin/stdout/stderr are properly connected for this command
            execvp(cmd->argv[0], cmd->argv);
            perror("execvp failed in pipeline");
            exit(1);
        }
    }


    // Parent process: close all pipe file descriptors and wait for all children
    // The parent must close pipe descriptors to allow proper pipeline termination
    // Without closing pipes, some commands may block waiting for input/output
    for (int i = 0; i < cmdlist->count - 1; i++) {
        close(pipes[i][0]);  // Close read end of each pipe
        close(pipes[i][1]);  // Close write end of each pipe
    }

    // Wait for all child processes in the pipeline to complete
    // This ensures proper cleanup and prevents zombie processes
    // We wait for all processes, not just the last one, for complete synchronization
    for (int c = 0; c < cmdlist->count; c++) {
        int status;
        waitpid(pids[c], &status, 0);
        
        // Optional: Check exit status of each command for debugging
        // In a production shell, you might want to track which commands failed
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            // Command exited with non-zero status (error)
            // This is normal for some commands (like grep with no matches)
            // but could be logged or handled based on shell requirements
        }
    }
}