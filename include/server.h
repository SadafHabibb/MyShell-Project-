// include/server.h
#ifndef SERVER_H
#define SERVER_H

#include "parser.h"

// Server configuration constants
#define PORT 8080              // Port number the server listens on
#define BUFFER_SIZE 4096       // Maximum size for command and output buffers
#define MAX_PENDING 5          // Maximum number of pending connections in listen queue

// ANSI color codes for formatted server output logging
#define COLOR_INFO "\033[1;34m"      // Blue for INFO messages
#define COLOR_RECEIVED "\033[1;33m"  // Yellow for RECEIVED messages
#define COLOR_EXECUTING "\033[1;35m" // Magenta for EXECUTING messages
#define COLOR_OUTPUT "\033[1;32m"    // Green for OUTPUT messages
#define COLOR_ERROR "\033[1;31m"     // Red for ERROR messages
#define COLOR_RESET "\033[0m"        // Reset to default color

/**
 * Starts the server and listens for client connections
 * Creates a socket, binds it to the specified port, and enters an infinite loop
 * accepting client connections and processing their commands
 */
void start_server();

/**
 * Handles communication with a single connected client
 * Receives commands from the client, executes them, and sends results back
 * 
 * @param client_socket - File descriptor for the connected client socket
 */
void handle_client(int client_socket);

/**
 * Executes a command string and captures its output
 * Integrates with Phase 1 parser and executor to run commands
 * Redirects stdout/stderr to capture all output from command execution
 * 
 * @param command - The command string to execute
 * @param output_buffer - Buffer to store the command output
 * @param buffer_size - Maximum size of the output buffer
 * @return 0 on success, -1 on error
 */
int execute_and_capture(const char *command, char *output_buffer, size_t buffer_size);

/**
 * Prints formatted log messages to server console with color coding
 * Used to display server activity including connections, commands, and errors
 * 
 * @param color - ANSI color code for the message
 * @param tag - Log level tag (e.g., "INFO", "ERROR")
 * @param message - The log message to display
 */
void log_message(const char *color, const char *tag, const char *message);

#endif