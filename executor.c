// src/executor.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "executor.h"

void execute_commands(CommandList *cmdlist) {
    for (int c = 0; c < cmdlist->count; c++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        } else if (pid == 0) {
            // Child process
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
