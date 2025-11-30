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
// ANSI COLOR CODES
// ============================================================================

#define COLOR_CYAN    "\033[1;36m"    // Bold Cyan for created
#define COLOR_GREEN   "\033[1;32m"    // Bold Green for started
#define COLOR_YELLOW  "\033[1;33m"    // Bold Yellow for waiting
#define COLOR_MAGENTA "\033[1;35m"    // Bold Magenta for running
#define COLOR_RED     "\033[1;31m"    // Bold Red for ended
#define COLOR_BLUE    "\033[1;37;46m" 
#define COLOR_RESET   "\033[0m"       // Reset color

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

int get_elapsed_seconds() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (int)(now.tv_sec - schedule_summary.start_time.tv_sec);
}

TaskType get_task_type(const char* command) {
    const char* shell_commands[] = {
        "ls", "pwd", "cd", "echo", "cat", "mkdir", "rmdir", "rm", "cp", "mv",
        "touch", "head", "tail", "grep", "find", "wc", "sort", "uniq", "date",
        "whoami", "hostname", "uname", "env", "export", "clear", "man", "help",
        "ps", "kill", "chmod", "chown", "df", "du", "tar", "gzip", "gunzip",
        NULL
    };
    
    char cmd_copy[4096];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    char* first_word = strtok(cmd_copy, " \t");
    if (first_word == NULL) return TASK_TYPE_SHELL;
    
    if (strncmp(first_word, "./", 2) == 0) return TASK_TYPE_PROGRAM;
    
    for (int i = 0; shell_commands[i] != NULL; i++) {
        if (strcmp(first_word, shell_commands[i]) == 0) return TASK_TYPE_SHELL;
    }
    
    return TASK_TYPE_SHELL;
}

int extract_burst_time(const char* command) {
    char cmd_copy[4096];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    char* token = strtok(cmd_copy, " \t");
    if (token == NULL) return DEFAULT_BURST_TIME;
    
    if (strstr(token, "demo") != NULL) {
        token = strtok(NULL, " \t");
        if (token != NULL) {
            int n = atoi(token);
            if (n > 0) return n;
        }
    }
    return DEFAULT_BURST_TIME;
}

// ============================================================================
// WAITING QUEUE FUNCTIONS
// ============================================================================

void init_waiting_queue() {
    memset(&waiting_queue, 0, sizeof(WaitingQueue));
    waiting_queue.count = 0;
    waiting_queue.last_selected_id = -1;
    
    pthread_mutex_init(&waiting_queue.mutex, NULL);
    pthread_cond_init(&waiting_queue.not_empty, NULL);
    pthread_cond_init(&waiting_queue.task_complete, NULL);
    
    memset(&schedule_summary, 0, sizeof(ScheduleSummary));
    gettimeofday(&schedule_summary.start_time, NULL);
}

void destroy_waiting_queue() {
    pthread_mutex_lock(&waiting_queue.mutex);
    for (int i = 0; i < waiting_queue.count; i++) {
        if (waiting_queue.tasks[i] != NULL) {
            free(waiting_queue.tasks[i]);
        }
    }
    waiting_queue.count = 0;
    pthread_mutex_unlock(&waiting_queue.mutex);
    
    pthread_mutex_destroy(&waiting_queue.mutex);
    pthread_cond_destroy(&waiting_queue.not_empty);
    pthread_cond_destroy(&waiting_queue.task_complete);
}

Task* create_task(const char* command, int client_num, int client_socket) {
    Task* task = (Task*)malloc(sizeof(Task));
    if (task == NULL) return NULL;
    
    pthread_mutex_lock(&task_id_mutex);
    task->task_id = client_num;
    pthread_mutex_unlock(&task_id_mutex);
    
    task->client_num = client_num;
    task->client_socket = client_socket;
    strncpy(task->command, command, sizeof(task->command) - 1);
    task->command[sizeof(task->command) - 1] = '\0';
    
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

int add_task_to_queue(Task* task) {
    pthread_mutex_lock(&waiting_queue.mutex);
    
    if (waiting_queue.count >= MAX_TASKS) {
        pthread_mutex_unlock(&waiting_queue.mutex);
        return -1;
    }
    
    // Reset start time only if system is completely idle
    pthread_mutex_lock(&scheduler_mutex);
    if (waiting_queue.count == 0 && schedule_summary.count == 0 && currently_running_task_id == -1) {
        gettimeofday(&schedule_summary.start_time, NULL);
    }
    pthread_mutex_unlock(&scheduler_mutex);
    
    waiting_queue.tasks[waiting_queue.count] = task;
    waiting_queue.count++;
    
    pthread_cond_signal(&waiting_queue.not_empty);
    pthread_mutex_unlock(&waiting_queue.mutex);
    return 0;
}

Task* remove_task_from_queue(int task_id) {
    pthread_mutex_lock(&waiting_queue.mutex);
    Task* removed_task = NULL;
    for (int i = 0; i < waiting_queue.count; i++) {
        if (waiting_queue.tasks[i] != NULL && waiting_queue.tasks[i]->task_id == task_id) {
            removed_task = waiting_queue.tasks[i];
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

void remove_client_tasks(int client_num) {
    pthread_mutex_lock(&waiting_queue.mutex);
    int i = 0;
    while (i < waiting_queue.count) {
        if (waiting_queue.tasks[i] != NULL && waiting_queue.tasks[i]->client_num == client_num) {
            free(waiting_queue.tasks[i]);
            for (int j = i; j < waiting_queue.count - 1; j++) {
                waiting_queue.tasks[j] = waiting_queue.tasks[j + 1];
            }
            waiting_queue.tasks[waiting_queue.count - 1] = NULL;
            waiting_queue.count--;
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&waiting_queue.mutex);
}

// ============================================================================
// SCHEDULING ALGORITHM
// ============================================================================

Task* select_next_task() {
    if (waiting_queue.count == 0) return NULL;
    
    Task* selected = NULL;
    int selected_index = -1;
    
    // 1. Shell commands have absolute priority
    for (int i = 0; i < waiting_queue.count; i++) {
        Task* task = waiting_queue.tasks[i];
        if (task != NULL && task->remaining_burst_time == SHELL_COMMAND_BURST) {
            if (task->task_id != waiting_queue.last_selected_id || waiting_queue.count == 1) {
                selected = task;
                selected_index = i;
                break;
            }
        }
    }
    
    // 2. Shortest Remaining Time First (SRTF) with "No Consecutive" Rule
    if (selected == NULL) {
        int shortest_time = -1;
        
        for (int i = 0; i < waiting_queue.count; i++) {
            Task* task = waiting_queue.tasks[i];
            if (task == NULL) continue;
            
            // Rule: No consecutive selection unless it's the only task
            if (task->task_id == waiting_queue.last_selected_id && waiting_queue.count > 1) {
                continue;
            }
            
            if (shortest_time == -1 || task->remaining_burst_time < shortest_time) {
                shortest_time = task->remaining_burst_time;
                selected = task;
                selected_index = i;
            }
        }
    }
    
    if (selected == NULL && waiting_queue.count > 0) {
        selected = waiting_queue.tasks[0];
        selected_index = 0;
    }
    
    if (selected != NULL && selected_index >= 0) {
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
// LOGGING
// ============================================================================

void log_task_state(Task* task, const char* state_msg) {
    char log_buffer[512];
    const char* color = COLOR_RESET;
    
    if (strcmp(state_msg, "created") == 0) color = COLOR_CYAN;
    else if (strcmp(state_msg, "started") == 0) color = COLOR_GREEN;
    else if (strcmp(state_msg, "waiting") == 0) color = COLOR_YELLOW;
    else if (strcmp(state_msg, "running") == 0) color = COLOR_MAGENTA;
    else if (strcmp(state_msg, "ended") == 0) color = COLOR_RED;
    
    pthread_mutex_lock(&scheduler_mutex);
    
    if (task->remaining_burst_time == SHELL_COMMAND_BURST) {
        snprintf(log_buffer, sizeof(log_buffer), "[%d]--- %s%s%s (-1)",
                 task->client_num, color, state_msg, COLOR_RESET);
    } else {
        snprintf(log_buffer, sizeof(log_buffer), "[%d]--- %s%s%s (%d)",
                 task->client_num, color, state_msg, COLOR_RESET, 
                 task->remaining_burst_time);
    }
    
    printf("%s\n", log_buffer);
    fflush(stdout);
    
    pthread_mutex_unlock(&scheduler_mutex);
}

void add_schedule_entry(int task_id) {
    pthread_mutex_lock(&scheduler_mutex);
    if (schedule_summary.count < MAX_TASKS * 10) {
        schedule_summary.entries[schedule_summary.count].task_id = task_id;
        schedule_summary.entries[schedule_summary.count].completion_time = get_elapsed_seconds();
        schedule_summary.count++;
    }
    pthread_mutex_unlock(&scheduler_mutex);
}

void print_schedule_summary() {
    pthread_mutex_lock(&scheduler_mutex);
    
    printf("\n%s", COLOR_BLUE);
    for (int i = 0; i < schedule_summary.count; i++) {
        if (i > 0) printf("-");
        printf("P%d-(%d)", schedule_summary.entries[i].task_id, 
               schedule_summary.entries[i].completion_time);
    }
    printf("%s\n", COLOR_RESET);
    fflush(stdout);
    
    // Reset summary for the next batch
    schedule_summary.count = 0;
    
    pthread_mutex_unlock(&scheduler_mutex);
}

// ============================================================================
// TASK EXECUTION
// ============================================================================

int execute_shell_command(Task* task) {
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) return -1;
    
    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]); close(pipe_fd[1]);
        return -1;
    } else if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        execlp("/bin/sh", "sh", "-c", task->command, NULL);
        exit(1);
    } else {
        close(pipe_fd[1]);
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

int execute_program_task(Task* task) {
    int quantum = (task->round_number == 0) ? FIRST_ROUND_QUANTUM : DEFAULT_QUANTUM;
    task->quantum = quantum;
    
    int iterations_to_run = (task->remaining_burst_time < quantum) ? 
                             task->remaining_burst_time : quantum;
    
    for (int i = 0; i < iterations_to_run; i++) {
        char line[128];
        snprintf(line, sizeof(line), "Demo %d/%d\n", 
                 task->current_iteration + 1, task->total_burst_time);
        
        send(task->client_socket, line, strlen(line), 0);
        
        sleep(1);
        
        task->current_iteration++;
        task->remaining_burst_time--;
        
        pthread_mutex_lock(&waiting_queue.mutex);
        int should_preempt = 0;
        
        for (int j = 0; j < waiting_queue.count; j++) {
            if (waiting_queue.tasks[j] != NULL && 
                waiting_queue.tasks[j]->remaining_burst_time == SHELL_COMMAND_BURST) {
                should_preempt = 1;
                break;
            }
        }
        
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
            task->round_number++; 
            return 0; // Preempted
        }
    }
    
    task->round_number++;
    
    if (task->remaining_burst_time <= 0) {
        return 1; // Completed
    }
    
    return 0; // Yielding
}

int execute_task(Task* task) {
    if (task->start_time.tv_sec == 0) {
        gettimeofday(&task->start_time, NULL);
    }
    
    task->state = TASK_RUNNING;
    currently_running_task_id = task->task_id;
    log_task_state(task, "running");
    
    int completed = 0;
    
    if (task->type == TASK_TYPE_SHELL) {
        execute_shell_command(task);
        completed = 1;
    } else {
        completed = execute_program_task(task);
    }
    
    currently_running_task_id = -1;
    
    if (completed) {
        gettimeofday(&task->end_time, NULL);
        task->state = TASK_ENDED;
        log_task_state(task, "ended");
        
        if (task->type != TASK_TYPE_SHELL) {
            add_schedule_entry(task->task_id);
        }
        
        if (task->type == TASK_TYPE_SHELL) {
            if (task->output_length > 0) {
                send(task->client_socket, task->output_buffer, task->output_length, 0);
                log_bytes_sent(task->client_num, task->output_length);
            } else {
                send(task->client_socket, "\n", 1, 0);
                log_bytes_sent(task->client_num, 1);
            }
        } else {
            int total_bytes = task->current_iteration * 12;
            log_bytes_sent(task->client_num, total_bytes);
        }
        
        pthread_mutex_lock(&waiting_queue.mutex);
        int queue_empty = (waiting_queue.count == 0);
        pthread_mutex_unlock(&waiting_queue.mutex);
        
        if (queue_empty && schedule_summary.count > 0) {
            print_schedule_summary();
        }
        
        return 1;
    } else {
        task->state = TASK_WAITING;
        log_task_state(task, "waiting");
        
        if (task->type != TASK_TYPE_SHELL) {
            add_schedule_entry(task->task_id);
        }
        
        return 0;
    }
}

// ============================================================================
// SCHEDULER THREAD
// ============================================================================

void* scheduler_thread(void* arg) {
    (void)arg;
    
    while (scheduler_running) {
        pthread_mutex_lock(&waiting_queue.mutex);
        
        while (waiting_queue.count == 0 && scheduler_running) {
            pthread_cond_wait(&waiting_queue.not_empty, &waiting_queue.mutex);
        }
        
        if (!scheduler_running) {
            pthread_mutex_unlock(&waiting_queue.mutex);
            break;
        }
        
        Task* task = select_next_task();
        
        pthread_mutex_unlock(&waiting_queue.mutex);
        
        if (task != NULL) {
            int completed = execute_task(task);
            
            if (completed) {
                free(task);
            } else {
                pthread_mutex_lock(&waiting_queue.mutex);
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

void start_scheduler() {
    scheduler_running = 1;
    pthread_t tid;
    if (pthread_create(&tid, NULL, scheduler_thread, NULL) != 0) {
        perror("Failed to create scheduler thread");
        return;
    }
    pthread_detach(tid);
}

void stop_scheduler() {
    scheduler_running = 0;
    pthread_mutex_lock(&waiting_queue.mutex);
    pthread_cond_signal(&waiting_queue.not_empty);
    pthread_mutex_unlock(&waiting_queue.mutex);
    
    if (schedule_summary.count > 0) {
        print_schedule_summary();
    }
}