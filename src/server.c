// src/server.c - Phase 3: Multithreaded Server
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
#include <pthread.h>
#include "../include/server.h"
#include "../include/parser.h"
#include "../include/executor.h"

// Global variables for thread management
static int client_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * prints formatted and color-coded log messages to the server console
 * now thread-safe using mutex to prevent interleaved output from multiple threads
 */
void log_message(const char *color, const char *tag, const char *message) {
    pthread_mutex_lock(&log_mutex);
    printf("%s[%s]%s %s\n", color, tag, COLOR_RESET, message);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

/**
 * executes a command and captures all its output (stdout and stderr)
 * this function integrates with Phase 1's parser and executor while redirecting
 * output to a buffer instead of the terminal
 * 
 * strategy:
 * 1. create a pipe to capture output
 * 2. fork a child process
 * 3. in child: redirect stdout/stderr to pipe, parse+execute command
 * 4. in parent: read from pipe into buffer
 */
int execute_and_capture(const char *command, char *output_buffer, size_t buffer_size) {
    // always capture: executor-level redirections (> and 2>) will override as needed
    // create a pipe for capturing command output
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("Pipe creation failed");
        return -1;
    }

    // fork a child process to parse and execute the command
    pid_t pid = fork();
    
    if (pid < 0) {
        // fork failed - clean up pipe and return error
        perror("Fork failed");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
        
    } else if (pid == 0) {
        // child process: redirect output to pipe, then parse and execute
        
        // close read end of pipe in child (we only write)
        close(pipe_fd[0]);
        
        // redirect both stdout and stderr to the write end of the pipe
        // this captures all output from the command execution
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        
        // parse and execute within the child so parser errors are captured too
        char command_copy[BUFFER_SIZE];
        strncpy(command_copy, command, BUFFER_SIZE - 1);
        command_copy[BUFFER_SIZE - 1] = '\0';

        CommandList *cmdlist_child = parse_input(command_copy);
        if (cmdlist_child != NULL) {
            execute_commands(cmdlist_child);
            free_command_list(cmdlist_child);
            exit(0);
        }
        
        // parse error already printed to stderr (captured); exit non-zero
        exit(1);
        
    } else {
        // parent process: read command output from pipe
        
        // close write end of pipe in parent (we only read)
        close(pipe_fd[1]);
        
        // read all output from the pipe into the buffer
        ssize_t total_read = 0;
        ssize_t bytes_read;
        
        // read in chunks until pipe is empty or buffer is full
        while ((bytes_read = read(pipe_fd[0], 
                                  output_buffer + total_read, 
                                  buffer_size - total_read - 1)) > 0) {
            total_read += bytes_read;
            
            // check if buffer is nearly full
            if (total_read >= buffer_size - 1) {
                break;
            }
        }
        
        // null-terminate the output buffer
        output_buffer[total_read] = '\0';
        
        // close read end of pipe
        close(pipe_fd[0]);
        
        // wait for child process to complete (we ignore exit status here;
        // the captured output contains any error messages)
        int status;
        waitpid(pid, &status, 0);

        return 0;
    }
}

/**
 * handles all communication with a single connected client
 * this function now runs in its own thread for concurrent client handling
 * receives commands, executes them, and sends results back to client
 */
void* handle_client_thread(void* arg) {
    ClientInfo* client_info = (ClientInfo*)arg;
    int client_socket = client_info->socket;
    int client_num = client_info->client_num;
    // int thread_id = client_info->thread_id;
    char client_ip[INET_ADDRSTRLEN];
    int client_port = client_info->port;
    
    // copy IP address before freeing client_info
    strncpy(client_ip, client_info->ip_address, INET_ADDRSTRLEN);
    
    // free the client info structure as we've copied all needed data
    free(client_info);
    
    // detach thread so resources are automatically released when it exits
    pthread_detach(pthread_self());
    
    char command_buffer[BUFFER_SIZE];
    char output_buffer[BUFFER_SIZE];
    char log_buffer[BUFFER_SIZE + 200];
    
    // main client communication loop
    while (1) {
        // clear buffers before each iteration
        memset(command_buffer, 0, BUFFER_SIZE);
        memset(output_buffer, 0, BUFFER_SIZE);
        
        // receive command from client
        ssize_t bytes_received = recv(client_socket, command_buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            break; //disconnect 
        }
        
        // null-terminate the received command
        command_buffer[bytes_received] = '\0';
        
        // remove trailing newline if present
        size_t len = strlen(command_buffer);
        if (len > 0 && command_buffer[len - 1] == '\n') {
            command_buffer[len - 1] = '\0';
        }
        
        // log the received command with client info
        snprintf(log_buffer, sizeof(log_buffer), 
                "[Client #%d - %s:%d] Received command: \"%s\"",
                client_num, client_ip, client_port, command_buffer);
        log_message(COLOR_RECEIVED, "RECEIVED", log_buffer);
        
        // check for exit command
        if (strcmp(command_buffer, "exit") == 0) {
            snprintf(log_buffer, sizeof(log_buffer),
                    "[Client #%d - %s:%d] Client requested disconnect. Closing connection.",
                    client_num, client_ip, client_port);
            log_message(COLOR_INFO, "INFO", log_buffer);
            
            const char *exit_msg = "Disconnected from server.\n";
            send(client_socket, exit_msg, strlen(exit_msg), 0);
            break;
        }
        
        // log command execution
        snprintf(log_buffer, sizeof(log_buffer),
                "[Client #%d - %s:%d] Executing command: \"%s\"",
                client_num, client_ip, client_port, command_buffer);
        log_message(COLOR_EXECUTING, "EXECUTING", log_buffer);
        
        // execute the command and capture its output
        int exec_result = execute_and_capture(command_buffer, output_buffer, BUFFER_SIZE);
        
        if (exec_result == -1) {
            // execution failed at system level (e.g., pipe/fork)
            snprintf(output_buffer, BUFFER_SIZE, "Server error: Failed to execute command\n");
            log_message(COLOR_ERROR, "ERROR", "System failure during command execution.");
        }
        
        // log that we're sending output back to client
        snprintf(log_buffer, sizeof(log_buffer),
                "[Client #%d - %s:%d] Sending output to client:",
                client_num, client_ip, client_port);
        log_message(COLOR_OUTPUT, "OUTPUT", log_buffer);
        
        // print captured output to server console **once**
        if (output_buffer[0] != '\0') {
            pthread_mutex_lock(&log_mutex);
            printf("%s", output_buffer);
            if (output_buffer[strlen(output_buffer) - 1] != '\n') {
                printf("\n");
            }
            fflush(stdout);
            pthread_mutex_unlock(&log_mutex);
        }
        
        // if the command produced no output at all, send a newline so the client
        // does not block waiting (keeps UX consistent with a local shell prompt return)
        if (output_buffer[0] == '\0') {
            output_buffer[0] = '\n';
            output_buffer[1] = '\0';
        }
        
        // send the output back to the client
        ssize_t bytes_sent = send(client_socket, output_buffer, strlen(output_buffer), 0);
        
        if (bytes_sent < 0) {
            snprintf(log_buffer, sizeof(log_buffer),
                    "[Client #%d - %s:%d] Failed to send output to client.",
                    client_num, client_ip, client_port);
            log_message(COLOR_ERROR, "ERROR", log_buffer);
            break;
        }
    }
    
    // close the client socket when done
    close(client_socket);
    
    snprintf(log_buffer, sizeof(log_buffer), "Client #%d disconnected.", client_num);
    log_message(COLOR_INFO, "INFO", log_buffer);
    
    return NULL;
}

/**
 * main server function that sets up the socket and listens for connections
 * now creates a new thread for each client connection to handle multiple clients simultaneously
 */
void start_server() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    // step 1: create TCP socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // step 2: set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // step 3: configure server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // step 4: bind socket to the address and port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // step 5: listen for incoming connections
    if (listen(server_socket, MAX_PENDING) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // server is now ready
    log_message(COLOR_INFO, "INFO", "Server started, waiting for client connections...");
    
    // main server loop: accept clients and create threads
    while (1) {
        // accept incoming client connection (blocking call)
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        // increment client counter (thread-safe)
        pthread_mutex_lock(&counter_mutex);
        client_counter++;
        int current_client_num = client_counter;
        pthread_mutex_unlock(&counter_mutex);
        
        // prepare client information structure to pass to thread
        ClientInfo* client_info = (ClientInfo*)malloc(sizeof(ClientInfo));
        if (client_info == NULL) {
            perror("Failed to allocate memory for client info");
            close(client_socket);
            continue;
        }
        
        client_info->socket = client_socket;
        client_info->client_num = current_client_num;
        client_info->thread_id = current_client_num;  // using client number as thread ID
        client_info->port = ntohs(client_addr.sin_port);
        strncpy(client_info->ip_address, inet_ntoa(client_addr.sin_addr), INET_ADDRSTRLEN);
        
        // log successful client connection
        char log_buffer[256];
        snprintf(log_buffer, sizeof(log_buffer),
                "Client #%d connected from %s:%d. Assigned to Thread-%d.",
                current_client_num, client_info->ip_address, client_info->port, current_client_num);
        log_message(COLOR_INFO, "INFO", log_buffer);
        
        // create a new thread to handle this client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client_thread, (void*)client_info) != 0) {
            perror("Failed to create thread");
            free(client_info);
            close(client_socket);
            continue;
        }
        
        // thread will handle everything from here, including freeing client_info
        // we continue to accept more clients
    }
    
    // clean up (never reached in current implementation)
    close(server_socket);
}

/**
 * main entry point for the server program
 */
int main() {
    printf("=== MyShell Remote Server - Phase 3 (Multithreaded) ===\n\n");
    start_server();
    return 0;
}