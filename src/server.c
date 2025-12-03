// src/server.c - Phase 4: Server with Scheduling Capabilities
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
#include "../include/scheduler.h"


//client management
static int client_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Prints formatted and color-coded log messages to the server console
 * Thread-safe using mutex to prevent interleaved output from multiple threads
 */
void log_message(const char *color, const char *tag, const char *message) {
    pthread_mutex_lock(&log_mutex);
    printf("%s[%s]%s %s\n", color, tag, COLOR_RESET, message);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

/**
 * Logs client connection in the Phase 4 format
 * Format: [client_num]<<< client connected
 */
void log_client_connected(int client_num) {
    pthread_mutex_lock(&log_mutex);
    printf("[%d]<<< client connected\n", client_num);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

/**
 * Logs a command received from a client
 * Format: [client_num]>>> command
 */
void log_command_received(int client_num, const char* command) {
    pthread_mutex_lock(&log_mutex);
    printf("[%d]>>> %s\n", client_num, command);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

/**
 * Logs bytes sent to a client
 * Format: [client_num]<<< N bytes sent
 */
void log_bytes_sent(int client_num, int bytes) {
    pthread_mutex_lock(&log_mutex);
    printf("[%d]<<< %d bytes sent\n", client_num, bytes);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

/**
 * Processes a command from a client by adding it to the scheduler queue
 * Shell commands get high priority (burst_time = -1)
 * Program commands are scheduled using RR + SJRF
 */
void process_command_with_scheduler(const char* command, int client_num, int client_socket) {
    //log the received command
    log_command_received(client_num, command);
    
    //create a task for this command
    Task* task = create_task(command, client_num, client_socket);
    if (task == NULL) {
        const char* error_msg = "Server error: Failed to create task\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
        return;
    }
    
    //log task creation
    log_task_state(task, "created");
    
    //for shell commands, mark as started immediately since they have priority
    if (task->type == TASK_TYPE_SHELL) {
        log_task_state(task, "started");
    } else {
        //for programs, log started then check if waiting is needed
        log_task_state(task, "started");
    }
    
    //add task to the waiting queue - scheduler will pick it up
    if (add_task_to_queue(task) != 0) {
        const char* error_msg = "Server error: Task queue is full\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
        free(task);
        return;
    }
}


/**
 * Handles all communication with a single connected client
 * Receives commands and passes them to the scheduler
 */
void* handle_client_thread(void* arg) {
    ClientInfo* client_info = (ClientInfo*)arg;
    int client_socket = client_info->socket;
    int client_num = client_info->client_num;
    char client_ip[INET_ADDRSTRLEN];
    (void)client_ip;  //may be used in future enhancements

    //copy IP address before freeing client_info
    strncpy(client_ip, client_info->ip_address, INET_ADDRSTRLEN);
    
    //free the client info structure as we've copied all needed data
    free(client_info);
    
    //detach thread so resources are automatically released when it exits
    pthread_detach(pthread_self());
    
    char command_buffer[BUFFER_SIZE];
    
    //main client communication loop
    while (1) {
        //clear buffer before each iteration
        memset(command_buffer, 0, BUFFER_SIZE);
        
        //receive command from client
        ssize_t bytes_received = recv(client_socket, command_buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            break; //client disconnected
        }
        
        //null-terminate the received command
        command_buffer[bytes_received] = '\0';
        
        //remove trailing newline if present
        size_t len = strlen(command_buffer);
        if (len > 0 && command_buffer[len - 1] == '\n') {
            command_buffer[len - 1] = '\0';
        }
        
        //skip empty commands
        if (strlen(command_buffer) == 0) {
            continue;
        }
        
        //check for exit command
        if (strcmp(command_buffer, "exit") == 0) {
            const char *exit_msg = "Disconnected from server.\n";
            send(client_socket, exit_msg, strlen(exit_msg), 0);
            break;
        }
        
        //process command through the scheduler
        process_command_with_scheduler(command_buffer, client_num, client_socket);
        
        //small delay to allow scheduler to process
        //this helps maintain order for quick successive commands
        usleep(100000); // 100ms
    }
    
    //remove all tasks for this client from the queue
    remove_client_tasks(client_num);
    
    //close the client socket when done
    close(client_socket);
    
    return NULL;
}

/**
 * Main server function that sets up the socket and listens for connections
 * Initializes the scheduler and creates threads for each client
 */
void start_server() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    //initialize the scheduler
    init_waiting_queue();
    start_scheduler();
    
    //create TCP socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    //set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    //configure server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    //bind socket to the address and port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    //listen for incoming connections
    if (listen(server_socket, MAX_PENDING) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    //server display startup banner
    printf("------------------------\n");
    printf("| Hello, Server Started |\n");
    printf("------------------------\n");
    fflush(stdout);
    
    //main server loop: accept clients and create threads
    while (1) {
        //accept incoming client connection (blocking call)
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        //increment client counter (thread-safe)
        pthread_mutex_lock(&counter_mutex);
        client_counter++;
        int current_client_num = client_counter;
        pthread_mutex_unlock(&counter_mutex);
        
        //log client connection
        log_client_connected(current_client_num);
        
        //prepare client information structure to pass to thread
        ClientInfo* client_info = (ClientInfo*)malloc(sizeof(ClientInfo));
        if (client_info == NULL) {
            perror("Failed to allocate memory for client info");
            close(client_socket);
            continue;
        }
        
        client_info->socket = client_socket;
        client_info->client_num = current_client_num;
        client_info->thread_id = current_client_num;
        client_info->port = ntohs(client_addr.sin_port);
        strncpy(client_info->ip_address, inet_ntoa(client_addr.sin_addr), INET_ADDRSTRLEN);
        
        //create a new thread to handle this client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client_thread, (void*)client_info) != 0) {
            perror("Failed to create thread");
            free(client_info);
            close(client_socket);
            continue;
        }
    }
    
    //cleanup (never reached in current implementation)
    stop_scheduler();
    destroy_waiting_queue();
    close(server_socket);
}

/**
 * Main entry point for the server program
 */
int main() {
    start_server();
    return 0;
}