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
 * Prints formatted and color-coded log messages to the server console
 * This provides visual feedback about server operations and helps with debugging
 * Each log type has a distinct color for easy identification
 */
void log_message(const char *color, const char *tag, const char *message) {
    printf("%s[%s]%s %s\n", color, tag, COLOR_RESET, message);
    fflush(stdout);  // Ensure immediate output to console
}

/**
 * Executes a command and captures all its output (stdout and stderr)
 * This function integrates with Phase 1's parser and executor while redirecting
 * output to a buffer instead of the terminal
 * 
 * Strategy:
 * 1. Create a pipe to capture output
 * 2. Fork a child process
 * 3. In child: redirect stdout/stderr to pipe, execute command
 * 4. In parent: read from pipe into buffer
 */
int execute_and_capture(const char *command, char *output_buffer, size_t buffer_size) {
    // Create a pipe for capturing command output
    // pipe_fd[0] is read end, pipe_fd[1] is write end
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("Pipe creation failed");
        return -1;
    }

    // Fork a child process to execute the command
    pid_t pid = fork();
    
    if (pid < 0) {
        // Fork failed - clean up pipe and return error
        perror("Fork failed");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
        
    } else if (pid == 0) {
        // Child process: execute the command with output redirected to pipe
        
        // Close read end of pipe in child (we only write)
        close(pipe_fd[0]);
        
        // Redirect both stdout and stderr to the write end of the pipe
        // This captures all output from the command execution
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);  // Close original fd after duplication
        
        // Parse the command using Phase 1 parser
        // Create a mutable copy of the command string since parse_input may modify it
        char command_copy[BUFFER_SIZE];
        strncpy(command_copy, command, BUFFER_SIZE - 1);
        command_copy[BUFFER_SIZE - 1] = '\0';
        
        CommandList *cmdlist = parse_input(command_copy);
        
        if (cmdlist == NULL) {
            // Parsing failed - print error and exit child
            fprintf(stderr, "Command parsing failed\n");
            exit(1);
        }
        
        // Execute the parsed command(s) using Phase 1 executor
        // Output will be captured through the redirected stdout/stderr
        execute_commands(cmdlist);
        
        // Clean up and exit child process
        free_command_list(cmdlist);
        exit(0);
        
    } else {
        // Parent process: read command output from pipe
        
        // Close write end of pipe in parent (we only read)
        close(pipe_fd[1]);
        
        // Read all output from the pipe into the buffer
        ssize_t total_read = 0;
        ssize_t bytes_read;
        
        // Read in chunks until pipe is empty or buffer is full
        while ((bytes_read = read(pipe_fd[0], 
                                  output_buffer + total_read, 
                                  buffer_size - total_read - 1)) > 0) {
            total_read += bytes_read;
            
            // Check if buffer is nearly full
            if (total_read >= buffer_size - 1) {
                break;
            }
        }
        
        // Null-terminate the output buffer
        output_buffer[total_read] = '\0';
        
        // Close read end of pipe
        close(pipe_fd[0]);
        
        // Wait for child process to complete
        int status;
        waitpid(pid, &status, 0);
        
        // Check if command execution was successful
        if (WIFEXITED(status)) {
            int exit_status = WEXITSTATUS(status);
            if (exit_status != 0) {
                // Command exited with error, but we still captured output
                return 0;  // Return success since we captured the error message
            }
        }
        
        return 0;  // Success
    }
}

/**
 * Handles all communication with a single connected client
 * This function implements the main server loop for one client:
 * 1. Receive command from client
 * 2. Log the received command
 * 3. Execute the command and capture output
 * 4. Send output back to client
 * 5. Repeat until client disconnects or sends "exit"
 */
void handle_client(int client_socket) {
    char command_buffer[BUFFER_SIZE];  // Buffer for receiving commands
    char output_buffer[BUFFER_SIZE];   // Buffer for command output
    char log_buffer[BUFFER_SIZE + 50]; // Buffer for log messages
    
    // Main client communication loop
    while (1) {
        // Clear buffers before each iteration
        memset(command_buffer, 0, BUFFER_SIZE);
        memset(output_buffer, 0, BUFFER_SIZE);
        
        // Receive command from client
        ssize_t bytes_received = recv(client_socket, command_buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            // Client disconnected or error occurred
            if (bytes_received == 0) {
                log_message(COLOR_INFO, "INFO", "Client disconnected.");
            } else {
                log_message(COLOR_ERROR, "ERROR", "Error receiving data from client.");
            }
            break;
        }
        
        // Null-terminate the received command
        command_buffer[bytes_received] = '\0';
        
        // Remove trailing newline if present
        size_t len = strlen(command_buffer);
        if (len > 0 && command_buffer[len - 1] == '\n') {
            command_buffer[len - 1] = '\0';
        }
        
        // Log the received command with color coding
        snprintf(log_buffer, sizeof(log_buffer), "Received command: \"%s\" from client.", command_buffer);
        log_message(COLOR_RECEIVED, "RECEIVED", log_buffer);
        
        // Check for exit command
        if (strcmp(command_buffer, "exit") == 0) {
            log_message(COLOR_INFO, "INFO", "Client requested exit.");
            const char *exit_msg = "Server: Goodbye!\n";
            send(client_socket, exit_msg, strlen(exit_msg), 0);
            break;
        }
        
        // Log command execution
        snprintf(log_buffer, sizeof(log_buffer), "Executing command: \"%s\"", command_buffer);
        log_message(COLOR_EXECUTING, "EXECUTING", log_buffer);
        
        // Execute the command and capture its output
        int exec_result = execute_and_capture(command_buffer, output_buffer, BUFFER_SIZE);
        
        if (exec_result == -1) {
            // Execution failed at system level (fork/pipe error)
            snprintf(output_buffer, BUFFER_SIZE, "Server error: Failed to execute command\n");
            log_message(COLOR_ERROR, "ERROR", "Command execution failed.");
        } else {
            // Check if command produced any output
            if (strlen(output_buffer) == 0) {
                // No output - command might not exist or produced no output
                snprintf(output_buffer, BUFFER_SIZE, "Command not found: %s\n", command_buffer);
                log_message(COLOR_ERROR, "ERROR", output_buffer);
            }
        }
        
        // Log that we're sending output back to client
        log_message(COLOR_OUTPUT, "OUTPUT", "Sending output to client:");
        
        // Print the actual output to server console (for demonstration)
        printf("%s", output_buffer);
        if (strlen(output_buffer) > 0 && output_buffer[strlen(output_buffer) - 1] != '\n') {
            printf("\n");  // Ensure newline after output
        }
        fflush(stdout);
        
        // Send the output back to the client
        ssize_t bytes_sent = send(client_socket, output_buffer, strlen(output_buffer), 0);
        
        if (bytes_sent < 0) {
            log_message(COLOR_ERROR, "ERROR", "Failed to send output to client.");
            break;
        }
        
        printf("\n");  // Blank line for readability between commands
    }
    
    // Close the client socket when done
    close(client_socket);
}

/**
 * Main server function that sets up the socket and listens for connections
 * This implements the standard socket programming workflow:
 * 1. Create socket
 * 2. Set socket options
 * 3. Bind to address and port
 * 4. Listen for connections
 * 5. Accept connections and handle clients
 */
void start_server() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    // Step 1: Create TCP socket
    // AF_INET = IPv4, SOCK_STREAM = TCP, 0 = default protocol
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Step 2: Set socket options to allow address reuse
    // This prevents "Address already in use" errors when restarting server
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // Step 3: Configure server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;           // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;   // Accept connections on any interface
    server_addr.sin_port = htons(PORT);         // Convert port to network byte order
    
    // Step 4: Bind socket to the address and port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // Step 5: Listen for incoming connections
    // MAX_PENDING is the backlog - maximum length of pending connection queue
    if (listen(server_socket, MAX_PENDING) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // Server is now ready - log startup message
    char start_msg[256];
    snprintf(start_msg, sizeof(start_msg), "Server started, waiting for client connections on port %d...", PORT);
    log_message(COLOR_INFO, "INFO", start_msg);
    
    // Main server loop: accept and handle clients
    while (1) {
        // Step 6: Accept incoming client connection (blocking call)
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;  // Continue listening for other clients
        }
        
        // Log successful client connection with IP address
        char client_info[256];
        snprintf(client_info, sizeof(client_info), "Client connected from %s:%d", 
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        log_message(COLOR_INFO, "INFO", client_info);
        
        // Handle the connected client
        // Note: This is a sequential server - it handles one client at a time
        // For concurrent handling, you would fork() here or use threads
        handle_client(client_socket);
        
        log_message(COLOR_INFO, "INFO", "Client connection closed.\n");
    }
    
    // Clean up (this code is never reached in the current implementation)
    close(server_socket);
}

/**
 * Main entry point for the server program
 */
int main() {
    printf("=== MyShell Remote Server - Phase 2 ===\n\n");
    start_server();
    return 0;
}