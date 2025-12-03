// src/client.c 
#include <stdio.h>      
#include <stdlib.h>     
#include <string.h>     
#include <unistd.h>    
#include <arpa/inet.h> 
#include <sys/select.h>
#include <errno.h>
#include <pthread.h>
#include "../include/client.h"

//flag to control the receive thread
static volatile int receiving = 0;
static int global_sock = -1;

/**
 * Thread function to continuously receive data from server
 * This allows the client to receive streaming output from demo programs
 * while still being able to send new commands
 */
void* receive_thread(void* arg) {
    int sock = *(int*)arg;
    char recv_buffer[CLIENT_BUFFER_SIZE];
    
    while (receiving) {
        //use select with a short timeout so we can check the receiving flag
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  //100ms timeout
        
        int select_result = select(sock + 1, &read_fds, NULL, NULL, &timeout);
        
        if (select_result > 0 && FD_ISSET(sock, &read_fds)) {
            memset(recv_buffer, 0, CLIENT_BUFFER_SIZE);
            ssize_t bytes_received = recv(sock, recv_buffer, CLIENT_BUFFER_SIZE - 1, 0);
            
            if (bytes_received > 0) {
                recv_buffer[bytes_received] = '\0';
                printf("%s", recv_buffer);
                fflush(stdout);
            } else if (bytes_received == 0) {
                printf("\nServer disconnected.\n");
                receiving = 0;
                break;
            }
        }
    }
    
    return NULL;
}

/**
 * Function to start the client and manage communication with the server
 * Updated for Phase 4 to handle streaming output from scheduled processes
 */
void start_client() {
    int sock;
    struct sockaddr_in server_addr;
    char send_buffer[CLIENT_BUFFER_SIZE];
    pthread_t recv_tid;

    //create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    global_sock = sock;

    //setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    //connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to a server\n");
    
    //start receive thread
    receiving = 1;
    if (pthread_create(&recv_tid, NULL, receive_thread, &sock) != 0) {
        perror("Failed to create receive thread");
        close(sock);
        exit(EXIT_FAILURE);
    }

    //main client loop
    while (receiving) {
        printf(">>> ");
        fflush(stdout);

        if (!fgets(send_buffer, CLIENT_BUFFER_SIZE, stdin)) {
            printf("\n");
            break;
        }

        //remove trailing newline
        size_t len = strlen(send_buffer);
        if (len > 0 && send_buffer[len - 1] == '\n') {
            send_buffer[len - 1] = '\0';
        }

        //skip empty commands
        if (strlen(send_buffer) == 0) {
            continue;
        }

        //handle exit command
        if (strcmp(send_buffer, "exit") == 0) {
            send(sock, send_buffer, strlen(send_buffer), 0);
            usleep(500000);  //wait for server response
            receiving = 0;
            break;
        }

        //send command to server
        if (send(sock, send_buffer, strlen(send_buffer), 0) < 0) {
            perror("Send failed");
            receiving = 0;
            break;
        }
        
        //give some time for server to respond before prompting again
        //for demo programs, the output will stream in via the receive thread
        usleep(200000);  //200ms delay
    }

    //stop receive thread
    receiving = 0;
    pthread_join(recv_tid, NULL);
    
    close(sock);
}

//main function
int main() {
    start_client();
    return 0;
}