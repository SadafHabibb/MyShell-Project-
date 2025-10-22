#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "client.h"

void start_client() {
    int sock;
    struct sockaddr_in server_addr;
    char send_buffer[CLIENT_BUFFER_SIZE];
    char recv_buffer[CLIENT_BUFFER_SIZE];
    ssize_t bytes_received;

    // Step 1: Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Step 2: Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Step 3: Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

    // Step 4: Main client loop
    while (1) {
        // Display prompt
        printf("$ ");
        fflush(stdout);

        // Read command from user
        if (!fgets(send_buffer, CLIENT_BUFFER_SIZE, stdin)) {
            printf("\n");
            break; // Handle Ctrl+D
        }

        // Remove trailing newline
        size_t len = strlen(send_buffer);
        if (len > 0 && send_buffer[len - 1] == '\n') {
            send_buffer[len - 1] = '\0';
        }

        // Check for exit command
        if (strcmp(send_buffer, "exit") == 0) {
            send(sock, send_buffer, strlen(send_buffer), 0);
            printf("Exiting client.\n");
            break;
        }

        // Send command to server
        if (send(sock, send_buffer, strlen(send_buffer), 0) < 0) {
            perror("Send failed");
            break;
        }

        // Receive output from server
        memset(recv_buffer, 0, CLIENT_BUFFER_SIZE);
        bytes_received = recv(sock, recv_buffer, CLIENT_BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            printf("Server disconnected.\n");
            break;
        }

        // Null-terminate and print server output
        recv_buffer[bytes_received] = '\0';
        printf("%s", recv_buffer);
        if (bytes_received > 0 && recv_buffer[bytes_received - 1] != '\n') {
            printf("\n"); // Ensure newline
        }
    }

    // Close socket
    close(sock);
}

int main() {
    start_client();
    return 0;
}
