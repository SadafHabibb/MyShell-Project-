// src/scheduler.c - Phase 4: Process Scheduler Implementation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../include/scheduler.h"
#include "../include/server.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

WaitingQueue waiting_queue;
ScheduleSummary schedule_summary;
pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t scheduler_cond = PTHREAD_COND_INITIALIZER;
int scheduler_running = 0;
int currently_running_task_id = -1;

static pthread_mutex_t task_id_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// LOGGING COLORS (matching server.h style)
// ============================================================================

#define COLOR_TASK_CREATE "\033[1;36m"   // cyan for task creation
#define COLOR_TASK_STATE "\033[1;33m"    // yellow for state changes
#define COLOR_SCHEDULER "\033[1;35m"     // magenta for scheduler actions
#define COLOR_SUMMARY "\033[1;34m"       // blue for summary

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Gets the current time in seconds since the scheduler started
 * Used for tracking task completion times in the summary
 */
int get_elapsed_seconds() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (int)(now.tv_sec - schedule_summary.start_time.tv_sec);
}

/**
 * Determines if a command is a shell command or a program
 * Shell commands execute immediately without scheduling
 * Programs can be preempted and scheduled using RR + SJRF
 */
TaskType get_task_type(const char* command) {
    // list of common shell commands that should run immediately
    const char* shell_commands[] = {
        "ls", "pwd", "cd", "echo", "cat", "mkdir", "rmdir", "rm", "cp", "mv",
        "touch", "head", "tail", "grep", "find", "wc", "sort", "uniq", "date",
        "whoami", "hostname", "uname", "env", "export", "clear", "man", "help",
        NULL
    };
    
    // make a copy of the command to extract the first word
    char cmd_copy[4096];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    // get the first word (command name)
    char* first_word = strtok(cmd_copy, " \t");
    if (first_word == NULL) {
        return TASK_TYPE_SHELL; // empty command, treat as shell
    }
    
    // check if it starts with "./" which indicates a program
    if (strncmp(first_word, "./", 2) == 0) {
        return TASK_TYPE_PROGRAM;
    }
    
    // check if it's in the list of shell commands
    for (int i = 0; shell_commands[i] != NULL; i++) {
        if (strcmp(first_word, shell_commands[i]) == 0) {
            return TASK_TYPE_SHELL;
        }
    }
    
    // default to shell command for unknown commands
    return TASK_TYPE_SHELL;
}

/**
 * Extracts the burst time (N value) from a command
 * For "./demo N" commands, extracts the N value
 * For other programs, returns default burst time
 */
int extract_burst_time(const char* command) {
    char cmd_copy[4096];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    // tokenize to get command parts
    char* token = strtok(cmd_copy, " \t");
    if (token == NULL) {
        return DEFAULT_BURST_TIME;
    }
    
    // check if it's the demo program
    if (strstr(token, "demo") != NULL) {
        // get the next token which should be N
        token = strtok(NULL, " \t");
        if (token != NULL) {
            int n = atoi(token);
            if (n > 0) {
                return n;
            }
        }
    }
    
    // for other programs, return default
    return DEFAULT_BURST_TIME;
}

// ============================================================================
// WAITING QUEUE FUNCTIONS
// ============================================================================

/**
 * Initializes the waiting queue and all synchronization primitives
 */
void init_waiting_queue() {
    memset(&waiting_queue, 0, sizeof(WaitingQueue));
    waiting_queue.count = 0;
    waiting_queue.last_selected_id = -1;
    
    pthread_mutex_init(&waiting_queue.mutex, NULL);
    pthread_cond_init(&waiting_queue.not_empty, NULL);
    pthread_cond_init(&waiting_queue.task_complete, NULL);
    
    // initialize schedule summary
    memset(&schedule_summary, 0, sizeof(ScheduleSummary));
    gettimeofday(&schedule_summary.start_time, NULL);
}

/**
 * Cleans up the waiting queue and frees all tasks
 */
void destroy_waiting_queue() {
    pthread_mutex_lock(&waiting_queue.mutex);
    
    // free all remaining tasks
    for (int i = 0; i < waiting_queue.count; i++) {
        if (waiting_queue.tasks[i] != NULL) {
            free(waiting_queue.tasks[i]);
            waiting_queue.tasks[i] = NULL;
        }
    }
    waiting_queue.count = 0;
    
    pthread_mutex_unlock(&waiting_queue.mutex);
    
    pthread_mutex_destroy(&waiting_queue.mutex);
    pthread_cond_destroy(&waiting_queue.not_empty);
    pthread_cond_destroy(&waiting_queue.task_complete);
}

/**
 * Creates a new task from a command
 */
Task* create_task(const char* command, int client_num, int client_socket) {
    Task* task = (Task*)malloc(sizeof(Task));
    if (task == NULL) {
        perror("Failed to allocate memory for task");
        return NULL;
    }
    
    // get unique task ID
    pthread_mutex_lock(&task_id_mutex);
    task->task_id = client_num; // use client number as task ID for display
    pthread_mutex_unlock(&task_id_mutex);
    
    // initialize task fields
    task->client_num = client_num;
    task->client_socket = client_socket;
    strncpy(task->command, command, sizeof(task->command) - 1);
    task->command[sizeof(task->command) - 1] = '\0';
    
    // determine task type and burst time
    task->type = get_task_type(command);
    task->state = TASK_CREATED;
    
    if (task->type == TASK_TYPE_SHELL) {
        task->total_burst_time = SHELL_COMMAND_BURST;
        task->remaining_burst_time = SHELL_COMMAND_BURST;
    } else {
        task->total_burst_time = extract_burst_time(command);
        task->remaining_burst_time = task->total_burst_time;
    }
    
    task->current_iteration = 0;
    task->round_number = 0;
    task->quantum = FIRST_ROUND_QUANTUM;
    
    gettimeofday(&task->arrival_time, NULL);
    memset(&task->start_time, 0, sizeof(struct timeval));
    memset(&task->end_time, 0, sizeof(struct timeval));
    
    task->output_buffer[0] = '\0';
    task->output_length = 0;
    
    return task;
}

/**
 * Adds a task to the waiting queue
 */
int add_task_to_queue(Task* task) {
    pthread_mutex_lock(&waiting_queue.mutex);
    
    if (waiting_queue.count >= MAX_TASKS) {
        pthread_mutex_unlock(&waiting_queue.mutex);
        return -1; // queue is full
    }
    
    waiting_queue.tasks[waiting_queue.count] = task;
    waiting_queue.count++;
    
    // signal that the queue is not empty
    pthread_cond_signal(&waiting_queue.not_empty);
    
    pthread_mutex_unlock(&waiting_queue.mutex);
    
    return 0;
}

/**
 * Removes a task from the queue by ID
 */
Task* remove_task_from_queue(int task_id) {
    pthread_mutex_lock(&waiting_queue.mutex);
    
    Task* removed_task = NULL;
    
    for (int i = 0; i < waiting_queue.count; i++) {
        if (waiting_queue.tasks[i] != NULL && waiting_queue.tasks[i]->task_id == task_id) {
            removed_task = waiting_queue.tasks[i];
            
            // shift remaining tasks down
            for (int j = i; j < waiting_queue.count - 1; j++) {
                waiting_queue.tasks[j] = waiting_queue.tasks[j + 1];
            }
            waiting_queue.tasks[waiting_queue.count - 1] = NULL;
            waiting_queue.count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&waiting_queue.mutex);
    
    return removed_task;
}

/**
 * Removes all tasks belonging to a specific client
 */
void remove_client_tasks(int client_num) {
    pthread_mutex_lock(&waiting_queue.mutex);
    
    int i = 0;
    while (i < waiting_queue.count) {
        if (waiting_queue.tasks[i] != NULL && waiting_queue.tasks[i]->client_num == client_num) {
            free(waiting_queue.tasks[i]);
            
            // shift remaining tasks down
            for (int j = i; j < waiting_queue.count - 1; j++) {
                waiting_queue.tasks[j] = waiting_queue.tasks[j + 1];
            }
            waiting_queue.tasks[waiting_queue.count - 1] = NULL;
            waiting_queue.count--;
            // don't increment i, check the new task at this position
        } else {
            i++;
        }
    }
    
    pthread_mutex_unlock(&waiting_queue.mutex);
}

// ============================================================================
// SCHEDULING ALGORITHM: Combined RR + SJRF
// ============================================================================

/**
 * Selects the next task to execute using the combined RR + SJRF algorithm
 * 
 * Algorithm:
 * 1. Shell commands (burst_time == -1) have highest priority and run immediately
 * 2. Among program tasks, select the one with shortest remaining time (SJRF)
 * 3. If remaining times are equal, use FCFS (first in queue)
 * 4. Same task cannot be selected twice consecutively (unless only task left)
 */
Task* select_next_task() {
    // this function assumes the caller holds waiting_queue.mutex
    
    if (waiting_queue.count == 0) {
        return NULL;
    }
    
    Task* selected = NULL;
    int selected_index = -1;
    
    // first pass: look for shell commands (highest priority)
    for (int i = 0; i < waiting_queue.count; i++) {
        Task* task = waiting_queue.tasks[i];
        if (task != NULL && task->remaining_burst_time == SHELL_COMMAND_BURST) {
            // shell command found - select it unless it was last selected AND there are other tasks
            if (task->task_id != waiting_queue.last_selected_id || waiting_queue.count == 1) {
                selected = task;
                selected_index = i;
                break;
            }
        }
    }
    
    // second pass: if no shell command, find shortest remaining job
    if (selected == NULL) {
        int shortest_time = -1;
        
        for (int i = 0; i < waiting_queue.count; i++) {
            Task* task = waiting_queue.tasks[i];
            if (task == NULL) continue;
            
            // skip if this was the last selected task (unless it's the only one)
            if (task->task_id == waiting_queue.last_selected_id && waiting_queue.count > 1) {
                continue;
            }
            
            // SJRF: select task with shortest remaining time
            // if times are equal, FCFS is naturally applied (first in array wins)
            if (shortest_time == -1 || task->remaining_burst_time < shortest_time) {
                shortest_time = task->remaining_burst_time;
                selected = task;
                selected_index = i;
            }
        }
    }
    
    // if we still have no selection (all tasks were the last selected), just pick the first one
    if (selected == NULL && waiting_queue.count > 0) {
        selected = waiting_queue.tasks[0];
        selected_index = 0;
    }
    
    // remove selected task from queue (it will be added back if not complete)
    if (selected != NULL && selected_index >= 0) {
        // shift remaining tasks down
        for (int j = selected_index; j < waiting_queue.count - 1; j++) {
            waiting_queue.tasks[j] = waiting_queue.tasks[j + 1];
        }
        waiting_queue.tasks[waiting_queue.count - 1] = NULL;
        waiting_queue.count--;
        
        waiting_queue.last_selected_id = selected->task_id;
    }
    
    return selected;
}

// ============================================================================
// TASK LOGGING
// ============================================================================

/**
 * Logs a task state change in the format specified by the project
 * Format: [client_num]--- state (remaining_time)
 */
void log_task_state(Task* task, const char* state_msg) {
    char log_buffer[256];
    
    pthread_mutex_lock(&scheduler_mutex);
    
    if (task->remaining_burst_time == SHELL_COMMAND_BURST) {
        snprintf(log_buffer, sizeof(log_buffer), "[%d]--- %s (-1)",
                 task->client_num, state_msg);
    } else {
        snprintf(log_buffer, sizeof(log_buffer), "[%d]--- %s (%d)",
                 task->client_num, state_msg, task->remaining_burst_time);
    }
    
    printf("%s\n", log_buffer);
    fflush(stdout);
    
    pthread_mutex_unlock(&scheduler_mutex);
}


// ============================================================================
// SCHEDULE SUMMARY
// ============================================================================

/**
 * Adds an entry to the scheduling summary
 */
void add_schedule_entry(int task_id) {
    pthread_mutex_lock(&scheduler_mutex);
    
    if (schedule_summary.count < MAX_TASKS * 10) {
        schedule_summary.entries[schedule_summary.count].task_id = task_id;
        schedule_summary.entries[schedule_summary.count].completion_time = get_elapsed_seconds();
        schedule_summary.count++;
    }
    
    pthread_mutex_unlock(&scheduler_mutex);
}

/**
 * Prints the scheduling summary in the format: P5-(3)-P7-(6)-P6-(13)...
 */
void print_schedule_summary() {
    pthread_mutex_lock(&scheduler_mutex);
    
    printf("\n%s", COLOR_SUMMARY);
    
    for (int i = 0; i < schedule_summary.count; i++) {
        if (i > 0) {
            printf("-");
        }
        printf("P%d-(%d)", schedule_summary.entries[i].task_id, 
               schedule_summary.entries[i].completion_time);
    }
    
    printf("%s\n", COLOR_RESET);
    fflush(stdout);
    
    pthread_mutex_unlock(&scheduler_mutex);
}

// ============================================================================
// TASK EXECUTION
// ============================================================================

/**
 * Executes a shell command and captures output
 */
int execute_shell_command(Task* task) {
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        return -1;
    }
    
    pid_t pid = fork();
    
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    } else if (pid == 0) {
        // child process
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        
        // execute the command using /bin/sh
        execlp("/bin/sh", "sh", "-c", task->command, NULL);
        exit(1);
    } else {
        // parent process
        close(pipe_fd[1]);
        
        // read output into task buffer
        ssize_t bytes = read(pipe_fd[0], task->output_buffer, sizeof(task->output_buffer) - 1);
        if (bytes > 0) {
            task->output_buffer[bytes] = '\0';
            task->output_length = bytes;
        } else {
            task->output_buffer[0] = '\0';
            task->output_length = 0;
        }
        
        close(pipe_fd[0]);
        waitpid(pid, NULL, 0);
    }
    
    return 0;
}

/**
 * Executes a program task for one quantum period
 * Returns 1 if task completed, 0 if needs more time
 * 
 * For demo programs, output is sent progressively (one line per second)
 * to match the expected output in the project screenshots
 */
int execute_program_task(Task* task) {
    // determine quantum based on round number
    int quantum = (task->round_number == 0) ? FIRST_ROUND_QUANTUM : DEFAULT_QUANTUM;
    task->quantum = quantum;
    
    // calculate how many iterations to run this quantum
    int iterations_to_run = (task->remaining_burst_time < quantum) ? 
                             task->remaining_burst_time : quantum;
    
    // simulate execution: run iterations with 1 second sleep each
    for (int i = 0; i < iterations_to_run; i++) {
        // create output line: "Demo X/Y"
        char line[128];
        snprintf(line, sizeof(line), "Demo %d/%d\n", 
                 task->current_iteration, task->total_burst_time);
        
        // send output immediately to client (one line at a time as per requirements)
        send(task->client_socket, line, strlen(line), 0);
        
        // simulate one unit of work (1 second)
        sleep(1);
        
        task->current_iteration++;
        task->remaining_burst_time--;
        
        // check if a higher priority task arrived (preemption check)
        // we check after each iteration to allow SJRF to preempt if needed
        pthread_mutex_lock(&waiting_queue.mutex);
        int should_preempt = 0;
        
        // check if there's a shell command waiting (always preempt for shell commands)
        for (int j = 0; j < waiting_queue.count; j++) {
            if (waiting_queue.tasks[j] != NULL && 
                waiting_queue.tasks[j]->remaining_burst_time == SHELL_COMMAND_BURST) {
                should_preempt = 1;
                break;
            }
        }
        
        // also check if there's a task with shorter remaining time (SJRF preemption)
        for (int j = 0; j < waiting_queue.count && !should_preempt; j++) {
            if (waiting_queue.tasks[j] != NULL && 
                waiting_queue.tasks[j]->remaining_burst_time > 0 &&
                waiting_queue.tasks[j]->remaining_burst_time < task->remaining_burst_time) {
                should_preempt = 1;
                break;
            }
        }
        
        pthread_mutex_unlock(&waiting_queue.mutex);
        
        if (should_preempt && task->remaining_burst_time > 0) {
            // preempt this task - increment round and return
            task->round_number++;
            return 0;
        }
    }
    
    // increment round number for next execution
    task->round_number++;
    
    // check if task is complete
    if (task->remaining_burst_time <= 0) {
        return 1; // task completed
    }
    
    return 0; // task needs more time
}

/**
 * Main task execution function
 */
int execute_task(Task* task) {
    // set first start time if not already set
    if (task->start_time.tv_sec == 0) {
        gettimeofday(&task->start_time, NULL);
    }
    
    task->state = TASK_RUNNING;
    currently_running_task_id = task->task_id;
    log_task_state(task, "running");
    
    int completed = 0;
    
    if (task->type == TASK_TYPE_SHELL) {
        // shell commands execute to completion immediately
        execute_shell_command(task);
        completed = 1;
    } else {
        // program tasks execute for one quantum
        completed = execute_program_task(task);
    }
    
    currently_running_task_id = -1;
    
    if (completed) {
        gettimeofday(&task->end_time, NULL);
        task->state = TASK_ENDED;
        log_task_state(task, "ended");
        add_schedule_entry(task->task_id);
        
        // for shell commands, send the output
        if (task->type == TASK_TYPE_SHELL) {
            if (task->output_length > 0) {
                send(task->client_socket, task->output_buffer, task->output_length, 0);
                log_bytes_sent(task->client_num, task->output_length);
            } else {
                // send empty response for commands with no output
                send(task->client_socket, "\n", 1, 0);
                log_bytes_sent(task->client_num, 1);
            }
        } else {
            // for program tasks, output was already sent progressively
            // just log the completion bytes
            int total_bytes = task->current_iteration * 12; // approximate bytes sent
            log_bytes_sent(task->client_num, total_bytes);
        }
        
        return 1;
    } else {
        // task needs more time - put back in queue
        task->state = TASK_WAITING;
        log_task_state(task, "waiting");
        
        return 0;
    }
}

// ============================================================================
// SCHEDULER THREAD
// ============================================================================

/**
 * Main scheduler thread function
 * Continuously selects and executes tasks from the waiting queue
 */
void* scheduler_thread(void* arg) {
    (void)arg; // unused
    
    while (scheduler_running) {
        pthread_mutex_lock(&waiting_queue.mutex);
        
        // wait for tasks to be available
        while (waiting_queue.count == 0 && scheduler_running) {
            pthread_cond_wait(&waiting_queue.not_empty, &waiting_queue.mutex);
        }
        
        if (!scheduler_running) {
            pthread_mutex_unlock(&waiting_queue.mutex);
            break;
        }
        
        // select next task
        Task* task = select_next_task();
        
        pthread_mutex_unlock(&waiting_queue.mutex);
        
        if (task != NULL) {
            // execute the task
            int completed = execute_task(task);
            
            if (completed) {
                // task is done, free it
                free(task);
            } else {
                // task needs more time, add back to queue
                pthread_mutex_lock(&waiting_queue.mutex);
                
                // add at end of queue (RR behavior)
                if (waiting_queue.count < MAX_TASKS) {
                    waiting_queue.tasks[waiting_queue.count] = task;
                    waiting_queue.count++;
                }
                
                pthread_mutex_unlock(&waiting_queue.mutex);
            }
        }
    }
    
    return NULL;
}

/**
 * Starts the scheduler thread
 */
void start_scheduler() {
    scheduler_running = 1;
    
    pthread_t tid;
    if (pthread_create(&tid, NULL, scheduler_thread, NULL) != 0) {
        perror("Failed to create scheduler thread");
        return;
    }
    
    pthread_detach(tid);
}

/**
 * Stops the scheduler thread
 */
void stop_scheduler() {
    scheduler_running = 0;
    
    // wake up the scheduler if it's waiting
    pthread_mutex_lock(&waiting_queue.mutex);
    pthread_cond_signal(&waiting_queue.not_empty);
    pthread_mutex_unlock(&waiting_queue.mutex);
}