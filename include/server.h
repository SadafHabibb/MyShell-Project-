
// // include/server.h - Phase 3: Multithreaded Server
// #ifndef SERVER_H
// #define SERVER_H

// #include "parser.h"
// #include <pthread.h>
// #include <netinet/in.h>

// // server configuration constants
// #define PORT 8080              // port number the server listens on
// #define BUFFER_SIZE 4096       // maximum size for command and output buffers
// #define MAX_PENDING 5          // maximum number of pending connections in listen queue

// // ANSI color codes for formatted server output logging
// #define COLOR_INFO "\033[1;34m"      // blue for INFO messages
// #define COLOR_RECEIVED "\033[1;33m"  // yellow for RECEIVED messages
// #define COLOR_EXECUTING "\033[1;35m" // magenta for EXECUTING messages
// #define COLOR_OUTPUT "\033[1;32m"    // green for OUTPUT messages
// #define COLOR_ERROR "\033[1;31m"     // red for ERROR messages
// #define COLOR_RESET "\033[0m"        // reset to default color

// /**
//  * structure to hold client connection information
//  * passed to each thread to identify and manage the client
//  */
// typedef struct {
//     int socket;                      // client socket file descriptor
//     int client_num;                  // sequential client number (1, 2, 3, ...)
//     int thread_id;                   // thread identifier
//     char ip_address[INET_ADDRSTRLEN]; // client IP address string
//     int port;                        // client port number
// } ClientInfo;

// /**
//  * starts the server and listens for client connections
//  * creates a socket, binds it to the specified port, and enters an infinite loop
//  * accepting client connections and spawning threads to handle them
//  */
// void start_server();

// /**
//  * thread function that handles communication with a single connected client
//  * receives commands from the client, executes them, and sends results back
//  * runs concurrently with other client handler threads
//  * 
//  * @param arg - pointer to ClientInfo structure containing client details
//  * @return NULL when client disconnects
//  */
// void* handle_client_thread(void* arg);

// /**
//  * executes a command string and captures its output
//  * integrates with Phase 1 parser and executor to run commands
//  * redirects stdout/stderr to capture all output from command execution
//  * 
//  * @param command - the command string to execute
//  * @param output_buffer - buffer to store the command output
//  * @param buffer_size - maximum size of the output buffer
//  * @return 0 on success, -1 on error
//  */
// int execute_and_capture(const char *command, char *output_buffer, size_t buffer_size);

// /**
//  * prints formatted log messages to server console with color coding
//  * thread-safe implementation using mutex to prevent interleaved output
//  * used to display server activity including connections, commands, and errors
//  * 
//  * @param color - ANSI color code for the message
//  * @param tag - log level tag (e.g., "INFO", "ERROR")
//  * @param message - the log message to display
//  */
// void log_message(const char *color, const char *tag, const char *message);

// #endif

// include/server.h - Phase 4: Server with Scheduling Capabilities
#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <netinet/in.h>

// ============================================================================
// SERVER CONFIGURATION CONSTANTS
// ============================================================================

#define PORT 8080              // port number the server listens on
#define BUFFER_SIZE 4096       // maximum size for command and output buffers
#define MAX_PENDING 5          // maximum number of pending connections in listen queue

// ============================================================================
// ANSI COLOR CODES FOR LOGGING
// ============================================================================

#define COLOR_INFO "\033[1;34m"      // blue for INFO messages
#define COLOR_RECEIVED "\033[1;33m"  // yellow for RECEIVED messages
#define COLOR_EXECUTING "\033[1;35m" // magenta for EXECUTING messages
#define COLOR_OUTPUT "\033[1;32m"    // green for OUTPUT messages
#define COLOR_ERROR "\033[1;31m"     // red for ERROR messages
#define COLOR_RESET "\033[0m"        // reset to default color

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * Structure to hold client connection information
 * Passed to each thread to identify and manage the client
 */
typedef struct {
    int socket;                       // client socket file descriptor
    int client_num;                   // sequential client number (1, 2, 3, ...)
    int thread_id;                    // thread identifier
    char ip_address[INET_ADDRSTRLEN]; // client IP address string
    int port;                         // client port number
} ClientInfo;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

/**
 * Starts the server and listens for client connections
 * Creates a socket, binds it to the specified port, and enters an infinite loop
 * accepting client connections and spawning threads to handle them
 * Also initializes and starts the scheduler
 */
void start_server();

/**
 * Thread function that handles communication with a single connected client
 * Receives commands from the client and passes them to the scheduler
 * Runs concurrently with other client handler threads
 * 
 * @param arg - pointer to ClientInfo structure containing client details
 * @return NULL when client disconnects
 */
void* handle_client_thread(void* arg);

/**
 * Processes a command from a client by creating a task and adding it to scheduler
 * Shell commands are given high priority (burst_time = -1)
 * Program commands are scheduled using the RR + SJRF algorithm
 * 
 * @param command - the command string to process
 * @param client_num - the client number submitting this command
 * @param client_socket - socket to send output back to client
 */
void process_command_with_scheduler(const char* command, int client_num, int client_socket);

/**
 * Prints formatted log messages to server console with color coding
 * Thread-safe implementation using mutex to prevent interleaved output
 * 
 * @param color - ANSI color code for the message
 * @param tag - log level tag (e.g., "INFO", "ERROR")
 * @param message - the log message to display
 */
void log_message(const char *color, const char *tag, const char *message);

/**
 * Logs client connection in Phase 4 format
 * Format: [client_num]<<< client connected
 *
 * @param client_num - the client number that connected
 */
void log_client_connected(int client_num);

/**
 * Logs a command received from a client
 * Format: [client_num]>>> command
 *
 * @param client_num - the client number
 * @param command - the command string received
 */
void log_command_received(int client_num, const char* command);

/**
 * Logs bytes sent to a client
 * Format: [client_num]<<< N bytes sent
 *
 * @param client_num - the client number
 * @param bytes - number of bytes sent
 */
void log_bytes_sent(int client_num, int bytes);

#endif // SERVER_H