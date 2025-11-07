#include <stdio.h>      
#include <stdlib.h>     
#include <string.h>     
#include <unistd.h>    
#include <arpa/inet.h> 
#include <sys/select.h>
#include <errno.h>
#include "../include/client.h"

//function to start the client and manage communication with the server
void start_client() {
    int sock;  //socket file descriptor
    struct sockaddr_in server_addr;  //structure to hold server IP address and port
    char send_buffer[CLIENT_BUFFER_SIZE];  //buffer to hold user input before sending
    char recv_buffer[CLIENT_BUFFER_SIZE];  //buffer to store server response
    ssize_t bytes_received;  //number of bytes received from server

    //create socket (AF_INET for IPv4, SOCK_STREAM for TCP connection)
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {  //if socket creation fails, socket() returns -1
        perror("Socket creation failed");
        exit(EXIT_FAILURE);  //exit program with failure
    }

    //setup server address structure
    memset(&server_addr, 0, sizeof(server_addr));  //clear the structure memory
    server_addr.sin_family = AF_INET;  //specify IPv4
    server_addr.sin_port = htons(SERVER_PORT);  //convert port number to network byte order

    //convert server IP string to binary and store it in server_addr
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sock);  //close socket before exiting
        exit(EXIT_FAILURE);
    }

    //connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    //notify connection success
    printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

    //main client loop to read, send, and receive data
    while (1) {
        //display command prompt
        printf("$ ");
        fflush(stdout);  //ensure prompt is displayed immediately

        //read user input from terminal
        if (!fgets(send_buffer, CLIENT_BUFFER_SIZE, stdin)) {
            //fgets returns NULL on EOF (Ctrl+D)
            printf("\n");
            break;
        }

        //remove trailing newline from user input
        size_t len = strlen(send_buffer);
        if (len > 0 && send_buffer[len - 1] == '\n') {
            send_buffer[len - 1] = '\0';
        }

        //skip empty commands
        if (strlen(send_buffer) == 0) {
            continue;
        }

        //if the user types "exit", close connection gracefully
        if (strcmp(send_buffer, "exit") == 0) {
            send(sock, send_buffer, strlen(send_buffer), 0);  //notify server
            
            //wait for server's goodbye message
            memset(recv_buffer, 0, CLIENT_BUFFER_SIZE);
            bytes_received = recv(sock, recv_buffer, CLIENT_BUFFER_SIZE - 1, 0);
            if (bytes_received > 0) {
                recv_buffer[bytes_received] = '\0';
                printf("%s", recv_buffer);
            }
            
            printf("Exiting client.\n");
            break;
        }

        //send user command to server
        if (send(sock, send_buffer, strlen(send_buffer), 0) < 0) {
            perror("Send failed");
            break;
        }

        //receive response from server
        //FIXED: Clear buffer and use a loop to ensure we get all data
        memset(recv_buffer, 0, CLIENT_BUFFER_SIZE);
        
        //set a timeout for receiving data
        struct timeval timeout;
        timeout.tv_sec = 5;  // 5 second timeout
        timeout.tv_usec = 0;
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        
        //wait for data to be available
        int select_result = select(sock + 1, &read_fds, NULL, NULL, &timeout);
        
        if (select_result < 0) {
            perror("Select failed");
            break;
        } else if (select_result == 0) {
            printf("Server response timeout.\n");
            continue;
        }
        
        //receive data from server
        bytes_received = recv(sock, recv_buffer, CLIENT_BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            //if server closed connection or error occurred
            if (bytes_received == 0) {
                printf("Server disconnected.\n");
            } else {
                perror("Receive failed");
            }
            break;
        }

        //null-terminate received data before printing
        recv_buffer[bytes_received] = '\0';
        printf("%s", recv_buffer);

        //ensure clean output formatting
        if (bytes_received > 0 && recv_buffer[bytes_received - 1] != '\n') {
            printf("\n");
        }
    }

    //close the socket connection before exiting
    close(sock);
}

//main function that starts the client
int main() {
    start_client();  //run the client logic
    return 0;  //exit program successfully
}