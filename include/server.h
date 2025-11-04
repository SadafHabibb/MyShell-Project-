// //include/server.h
// #ifndef SERVER_H
// #define SERVER_H

// #include "parser.h"

// //server configuration constants
// #define PORT 8080              //port number the server listens on
// #define BUFFER_SIZE 4096       //maximum size for command and output buffers
// #define MAX_PENDING 5          //maximum number of pending connections in listen queue

// //ANSI color codes for formatted server output logging
// #define COLOR_INFO "\033[1;34m"      //blue for INFO messages
// #define COLOR_RECEIVED "\033[1;33m"  //yellow for RECEIVED messages
// #define COLOR_EXECUTING "\033[1;35m" //magenta for EXECUTING messages
// #define COLOR_OUTPUT "\033[1;32m"    //green for OUTPUT messages
// #define COLOR_ERROR "\033[1;31m"     //red for ERROR messages
// #define COLOR_RESET "\033[0m"        //reset to default color

// /**
//  *starts the server and listens for client connections
//  *creates a socket, binds it to the specified port, and enters an infinite loop
//  * accepting client connections and processing their commands
//  */
// void start_server();

// /**
//  *handles communication with a single connected client
//  *receives commands from the client, executes them, and sends results back
//  * 
//  * @param client_socket -file descriptor for the connected client socket
//  */
// void handle_client(int client_socket);

// /**
//  *executes a command string and captures its output
//  *integrates with Phase 1 parser and executor to run commands
//  *redirects stdout/stderr to capture all output from command execution
//  * 
//  * @param command -the command string to execute
//  * @param output_buffer -buffer to store the command output
//  * @param buffer_size -maximum size of the output buffer
//  * @return 0 on success, -1 on error
//  */
// int execute_and_capture(const char *command, char *output_buffer, size_t buffer_size);

// /**
//  *prints formatted log messages to server console with color coding
//  *used to display server activity including connections, commands, and errors
//  * 
//  * @param color -ANSI color code for the message
//  * @param tag -log level tag (e.g., "INFO", "ERROR")
//  * @param message -the log message to display
//  */
// void log_message(const char *color, const char *tag, const char *message);

// #endif

// include/server.h - Phase 3: Multithreaded Server
#ifndef SERVER_H
#define SERVER_H

#include "parser.h"
#include <pthread.h>
#include <netinet/in.h>

// server configuration constants
#define PORT 8080              // port number the server listens on
#define BUFFER_SIZE 4096       // maximum size for command and output buffers
#define MAX_PENDING 5          // maximum number of pending connections in listen queue

// ANSI color codes for formatted server output logging
#define COLOR_INFO "\033[1;34m"      // blue for INFO messages
#define COLOR_RECEIVED "\033[1;33m"  // yellow for RECEIVED messages
#define COLOR_EXECUTING "\033[1;35m" // magenta for EXECUTING messages
#define COLOR_OUTPUT "\033[1;32m"    // green for OUTPUT messages
#define COLOR_ERROR "\033[1;31m"     // red for ERROR messages
#define COLOR_RESET "\033[0m"        // reset to default color

/**
 * structure to hold client connection information
 * passed to each thread to identify and manage the client
 */
typedef struct {
    int socket;                      // client socket file descriptor
    int client_num;                  // sequential client number (1, 2, 3, ...)
    int thread_id;                   // thread identifier
    char ip_address[INET_ADDRSTRLEN]; // client IP address string
    int port;                        // client port number
} ClientInfo;

/**
 * starts the server and listens for client connections
 * creates a socket, binds it to the specified port, and enters an infinite loop
 * accepting client connections and spawning threads to handle them
 */
void start_server();

/**
 * thread function that handles communication with a single connected client
 * receives commands from the client, executes them, and sends results back
 * runs concurrently with other client handler threads
 * 
 * @param arg - pointer to ClientInfo structure containing client details
 * @return NULL when client disconnects
 */
void* handle_client_thread(void* arg);

/**
 * executes a command string and captures its output
 * integrates with Phase 1 parser and executor to run commands
 * redirects stdout/stderr to capture all output from command execution
 * 
 * @param command - the command string to execute
 * @param output_buffer - buffer to store the command output
 * @param buffer_size - maximum size of the output buffer
 * @return 0 on success, -1 on error
 */
int execute_and_capture(const char *command, char *output_buffer, size_t buffer_size);

/**
 * prints formatted log messages to server console with color coding
 * thread-safe implementation using mutex to prevent interleaved output
 * used to display server activity including connections, commands, and errors
 * 
 * @param color - ANSI color code for the message
 * @param tag - log level tag (e.g., "INFO", "ERROR")
 * @param message - the log message to display
 */
void log_message(const char *color, const char *tag, const char *message);

#endif