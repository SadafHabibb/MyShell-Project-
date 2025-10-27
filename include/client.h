#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>

//client configuration constants
#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"   //localhost, change if server is remote
#define CLIENT_BUFFER_SIZE 4096 //maximum size for sending/receiving commands

/**
 * Starts the client program
 * Connects to the server and enters a loop:
 * 1. Displays prompt
 * 2. Reads user input
 * 3. Sends command to server
 * 4. Receives output and displays it
 */
void start_client();

#endif
