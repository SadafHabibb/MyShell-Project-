// src/server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../include/server.h"
#include "../include/parser.h"
#include "../include/executor.h"

/**
 * prints formatted and color-coded log messages to the server console
 * this provides visual feedback about server operations and helps with debugging
 * each log type has a distinct color for easy identification
 */
void log_message(const char *color, const char *tag, const char *message) {
    printf("%s[%s]%s %s\n", color, tag, COLOR_RESET, message);
    fflush(stdout);  //ensure immediate output to console
}

/**
 * executes a command and captures all its output (stdout and stderr)
 * this function integrates with Phase 1's parser and executor while redirecting
 * output to a buffer instead of the terminal
 * 
 * strategy:
 * 1. create a pipe to capture output
 * 2. fork a child process
 * 3. in child: redirect stdout/stderr to pipe, execute command
 * 4. in parent: read from pipe into buffer
 */
int execute_and_capture(const char *command, char *output_buffer, size_t buffer_size) {
    //check if command has output or error redirection (>, 2>)
    //these commands should execute normally without output capture
    //because the redirection itself will handle the output
    int has_output_redirect = (strstr(command, ">") != NULL);
    
    //parse the command first to check for redirections
    char command_copy[BUFFER_SIZE];
    strncpy(command_copy, command, BUFFER_SIZE - 1);
    command_copy[BUFFER_SIZE - 1] = '\0';
    
    CommandList *cmdlist = parse_input(command_copy);
    if (cmdlist == NULL) {
        snprintf(output_buffer, buffer_size, "Command parsing failed\n");
        return -1;
    }
    
    //check if any command in the list has output or error redirection
    int needs_capture = 1;  //default: capture output
    for (int i = 0; i < cmdlist->count; i++) {
        if (cmdlist->commands[i].output_file != NULL || 
            cmdlist->commands[i].error_file != NULL) {
            needs_capture = 0;  //don't capture if redirecting to file
            break;
        }
    }
    
    //if command redirects output to file, execute without capturing
    //the file will contain the output, so we just confirm execution
    if (!needs_capture) {
        pid_t pid = fork();
        
        if (pid < 0) {
            free_command_list(cmdlist);
            snprintf(output_buffer, buffer_size, "Fork failed\n");
            return -1;
        } else if (pid == 0) {
            //child: execute command normally (output goes to file)
            execute_commands(cmdlist);
            free_command_list(cmdlist);
            exit(0);
        } else {
            //parent: wait and send confirmation message
            int status;
            waitpid(pid, &status, 0);
            free_command_list(cmdlist);
            
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                snprintf(output_buffer, buffer_size, "Command executed successfully\n");
            } else {
                snprintf(output_buffer, buffer_size, "Command failed\n");
            }
            return 0;
        }
    }
    
    //standard output capture path (for commands without file redirection)
    //create a pipe for capturing command output
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("Pipe creation failed");
        free_command_list(cmdlist);
        return -1;
    }

    //fork a child process to execute the command
    pid_t pid = fork();
    
    if (pid < 0) {
        //fork failed - clean up pipe and return error
        perror("Fork failed");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        free_command_list(cmdlist);
        return -1;
        
    } else if (pid == 0) {
        //child process: execute the command with output redirected to pipe
        
        //close read end of pipe in child (we only write)
        close(pipe_fd[0]);
        
        //redirect both stdout and stderr to the write end of the pipe
        //this captures all output from the command execution
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);  // Close original fd after duplication
        
        //execute the parsed command(s) using Phase 1 executor
        //output will be captured through the redirected stdout/stderr
        execute_commands(cmdlist);
        
        //clean up and exit child process
        free_command_list(cmdlist);
        exit(0);
        
    } else {
        //parent process: read command output from pipe
        
        //close write end of pipe in parent (we only read)
        close(pipe_fd[1]);
        
        //read all output from the pipe into the buffer
        ssize_t total_read = 0;
        ssize_t bytes_read;
        
        //read in chunks until pipe is empty or buffer is full
        while ((bytes_read = read(pipe_fd[0], 
                                  output_buffer + total_read, 
                                  buffer_size - total_read - 1)) > 0) {
            total_read += bytes_read;
            
            //check if buffer is nearly full
            if (total_read >= buffer_size - 1) {
                break;
            }
        }
        
        //null-terminate the output buffer
        output_buffer[total_read] = '\0';
        
        //close read end of pipe
        close(pipe_fd[0]);
        
        //wait for child process to complete
        int status;
        waitpid(pid, &status, 0);
        
        //free the command list
        free_command_list(cmdlist);
        
        //check if command execution was successful
        if (WIFEXITED(status)) {
            int exit_status = WEXITSTATUS(status);
            if (exit_status != 0) {
                //command exited with error, but we still captured output
                return 0;  //return success since we captured the error message
            }
        }
        
        return 0;  //success
    }
}

/**
 * handles all communication with a single connected client
 * this function implements the main server loop for one client:
 * 1.receive command from client
 * 2.log the received command
 * 3.execute the command and capture output
 * 4.send output back to client
 * 5.repeat until client disconnects or sends "exit"
 */
void handle_client(int client_socket) {
    char command_buffer[BUFFER_SIZE];  //buffer for receiving commands
    char output_buffer[BUFFER_SIZE];   //buffer for command output
    char log_buffer[BUFFER_SIZE + 50]; //buffer for log messages
    
    //main client communication loop
    while (1) {
        //clear buffers before each iteration
        memset(command_buffer, 0, BUFFER_SIZE);
        memset(output_buffer, 0, BUFFER_SIZE);
        
        //receive command from client
        ssize_t bytes_received = recv(client_socket, command_buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            //client disconnected or error occurred
            if (bytes_received == 0) {
                log_message(COLOR_INFO, "INFO", "Client disconnected.");
            } else {
                log_message(COLOR_ERROR, "ERROR", "Error receiving data from client.");
            }
            break;
        }
        
        //null-terminate the received command
        command_buffer[bytes_received] = '\0';
        
        //remove trailing newline if present
        size_t len = strlen(command_buffer);
        if (len > 0 && command_buffer[len - 1] == '\n') {
            command_buffer[len - 1] = '\0';
        }
        
        //log the received command with color coding
        snprintf(log_buffer, sizeof(log_buffer), "Received command: \"%s\" from client.", command_buffer);
        log_message(COLOR_RECEIVED, "RECEIVED", log_buffer);
        
        //check for exit command
        if (strcmp(command_buffer, "exit") == 0) {
            log_message(COLOR_INFO, "INFO", "Client requested exit.");
            const char *exit_msg = "Server: Goodbye!\n";
            send(client_socket, exit_msg, strlen(exit_msg), 0);
            break;
        }
        
        //log command execution
        snprintf(log_buffer, sizeof(log_buffer), "Executing command: \"%s\"", command_buffer);
        log_message(COLOR_EXECUTING, "EXECUTING", log_buffer);
        
        //execute the command and capture its output
        int exec_result = execute_and_capture(command_buffer, output_buffer, BUFFER_SIZE);
        
        if (exec_result == -1) {
            //execution failed at system level (fork/pipe error)
            snprintf(output_buffer, BUFFER_SIZE, "Server error: Failed to execute command\n");
            log_message(COLOR_ERROR, "ERROR", "Command execution failed.");
        } else {
            //check if command produced any output
            if (strlen(output_buffer) == 0) {
                //no output, command might not exist or produced no output
                snprintf(output_buffer, BUFFER_SIZE, "Command not found: %s\n", command_buffer);
                log_message(COLOR_ERROR, "ERROR", output_buffer);
            }
        }
        
        //log that we're sending output back to client
        log_message(COLOR_OUTPUT, "OUTPUT", "Sending output to client:");
        
        //print the actual output to server console (for demonstration)
        printf("%s", output_buffer);
        if (strlen(output_buffer) > 0 && output_buffer[strlen(output_buffer) - 1] != '\n') {
            printf("\n");  //ensure newline after output
        }
        fflush(stdout);
        
        //send the output back to the client
        ssize_t bytes_sent = send(client_socket, output_buffer, strlen(output_buffer), 0);
        
        if (bytes_sent < 0) {
            log_message(COLOR_ERROR, "ERROR", "Failed to send output to client.");
            break;
        }
        
        printf("\n");  //blank line for readability between commands
    }
    
    //close the client socket when done
    close(client_socket);
}

/**
 * main server function that sets up the socket and listens for connections
 * this implements the standard socket programming workflow:
 * 1.create socket
 * 2.set socket options
 * 3.bind to address and port
 * 4.listen for connections
 * 5.accept connections and handle clients
 */
void start_server() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    //step 1: create TCP socket
    //AF_INET = IPv4, SOCK_STREAM = TCP, 0 = default protocol
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    //step 2:set socket options to allow address reuse
    //this prevents "Address already in use" errors when restarting server
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    //step 3:configure server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;           //IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;   //accept connections on any interface
    server_addr.sin_port = htons(PORT);         //convert port to network byte order
    
    //bind socket to the address and port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    //step 5: listen for incoming connections
    //MAX_PENDING is the backlog - maximum length of pending connection queue
    if (listen(server_socket, MAX_PENDING) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    //server is now ready - log startup message
    char start_msg[256];
    snprintf(start_msg, sizeof(start_msg), "Server started, waiting for client connections on port %d...", PORT);
    log_message(COLOR_INFO, "INFO", start_msg);
    
    //main server loop: accept and handle clients
    while (1) {
        //step 6: accept incoming client connection (blocking call)
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;  //continue listening for other clients
        }
        
        //log successful client connection with IP address
        char client_info[256];
        snprintf(client_info, sizeof(client_info), "Client connected from %s:%d", 
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        log_message(COLOR_INFO, "INFO", client_info);
        
        //handle the connected client
        //note: this is a sequential server - it handles one client at a time
        //for concurrent handling, you would fork() here or use threads
        handle_client(client_socket);
        
        log_message(COLOR_INFO, "INFO", "Client connection closed.\n");
    }
    
    //clean up (this code is never reached in the current implementation)
    close(server_socket);
}

/**
 *main entry point for the server program
 */
int main() {
    printf("=== MyShell Remote Server - Phase 2 ===\n\n");
    start_server();
    return 0;
}