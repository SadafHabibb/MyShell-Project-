#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "executor.h"
#include "parser.h"

void execute_commands(CommandList *cmdlist) {
    if (!cmdlist) return;

    int num_cmds = cmdlist->count;
    int pipefd[2*num_cmds]; //max pipes needed

    //create pipes
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipefd + i*2) < 0) {
            perror("pipe");
            return;
        }
    }

    for (int i = 0; i < num_cmds; i++) {
        Command cmd = cmdlist->commands[i];
        if (!cmd.argv || !cmd.argv[0]) {
            fprintf(stderr, "Error: Empty command cannot execute\n");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) { // child
            //input redirection
            if (cmd.input_file) {
                int fd = open(cmd.input_file, O_RDONLY);
                if (fd < 0) { perror(cmd.input_file); exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            //output redirection
            if (cmd.output_file) {
                int fd = open(cmd.output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror(cmd.output_file); exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            //error redirection
            if (cmd.error_file) {
                int fd = open(cmd.error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror(cmd.error_file); exit(1); }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            //set up pipes
            if (i != 0) { //not first command
                dup2(pipefd[(i-1)*2], STDIN_FILENO);
            }
            if (i != num_cmds - 1) { //not last command
                dup2(pipefd[i*2 + 1], STDOUT_FILENO);
            }

            //close all pipe fds
            for (int j = 0; j < 2*(num_cmds-1); j++)
                close(pipefd[j]);

            execvp(cmd.argv[0], cmd.argv);
            perror("execvp failed");
            exit(1);
        } else if (pid < 0) {
            perror("fork failed");
            return;
        }
    }

    //close all pipes in parent
    for (int i = 0; i < 2*(num_cmds-1); i++)
        close(pipefd[i]);

    //wait for all children
    for (int i = 0; i < num_cmds; i++)
        wait(NULL);
}
