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



WaitingQueue waiting_queue;
ScheduleSummary schedule_summary;
pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t scheduler_cond = PTHREAD_COND_INITIALIZER;
int scheduler_running = 0;
int currently_running_task_id = -1;

static pthread_mutex_t task_id_mutex = PTHREAD_MUTEX_INITIALIZER;


#define COLOR_CYAN    "\033[1;36m"    //bold Cyan for created
#define COLOR_GREEN   "\033[1;32m"    //bold Green for started
#define COLOR_YELLOW  "\033[1;33m"    //bold Yellow for waiting
#define COLOR_MAGENTA "\033[1;35m"    //bold Magenta for running
#define COLOR_RED     "\033[1;31m"    //bold Red for ended
#define COLOR_BLUE    "\033[1;37;46m" 
#define COLOR_RESET   "\033[0m"       //reset color


//calculate how many seconds have elapsed since scheduler started
int get_elapsed_seconds() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (int)(now.tv_sec - schedule_summary.start_time.tv_sec);
}

//determine if command is a shell builtin or a program
TaskType get_task_type(const char* command) {
    //list of all shell commands that get highest priority
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

    //extract first token of command
    char* first_word = strtok(cmd_copy, " \t");
    if (first_word == NULL) return TASK_TYPE_SHELL;

    //programs always start with ./
    if (strncmp(first_word, "./", 2) == 0) return TASK_TYPE_PROGRAM;

    //check if it matches any shell command
    for (int i = 0; shell_commands[i] != NULL; i++) {
        if (strcmp(first_word, shell_commands[i]) == 0) return TASK_TYPE_SHELL;
    }

    //default to shell if unknown
    return TASK_TYPE_SHELL;
}

//extract execution time from demo command
int extract_burst_time(const char* command) {
    char cmd_copy[4096];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    //get first token
    char* token = strtok(cmd_copy, " \t");
    if (token == NULL) return DEFAULT_BURST_TIME;

    //if this is a demo program, extract the duration argument
    if (strstr(token, "demo") != NULL) {
        token = strtok(NULL, " \t");
        if (token != NULL) {
            int n = atoi(token);
            //return the duration or default if invalid
            if (n > 0) return n;
        }
    }
    return DEFAULT_BURST_TIME;
}



//initialize queue, mutexes, and condition variables
void init_waiting_queue() {
    memset(&waiting_queue, 0, sizeof(WaitingQueue));
    waiting_queue.count = 0;
    waiting_queue.last_selected_id = -1;

    //create synchronization primitives for thread-safe access
    pthread_mutex_init(&waiting_queue.mutex, NULL);
    pthread_cond_init(&waiting_queue.not_empty, NULL);
    pthread_cond_init(&waiting_queue.task_complete, NULL);

    //initialize schedule summary with start time
    memset(&schedule_summary, 0, sizeof(ScheduleSummary));
    gettimeofday(&schedule_summary.start_time, NULL);
}

//clean up queue and all synchronization objects
void destroy_waiting_queue() {
    pthread_mutex_lock(&waiting_queue.mutex);
    //free all remaining tasks in queue
    for (int i = 0; i < waiting_queue.count; i++) {
        if (waiting_queue.tasks[i] != NULL) {
            free(waiting_queue.tasks[i]);
        }
    }
    waiting_queue.count = 0;
    pthread_mutex_unlock(&waiting_queue.mutex);

    //destroy all synchronization primitives
    pthread_mutex_destroy(&waiting_queue.mutex);
    pthread_cond_destroy(&waiting_queue.not_empty);
    pthread_cond_destroy(&waiting_queue.task_complete);
}

//create a new task from a command string
Task* create_task(const char* command, int client_num, int client_socket) {
    Task* task = (Task*)malloc(sizeof(Task));
    if (task == NULL) return NULL;

    //assign unique task id
    pthread_mutex_lock(&task_id_mutex);
    task->task_id = client_num;
    pthread_mutex_unlock(&task_id_mutex);

    //store client info
    task->client_num = client_num;
    task->client_socket = client_socket;
    strncpy(task->command, command, sizeof(task->command) - 1);
    task->command[sizeof(task->command) - 1] = '\0';

    //determine if shell command or program
    task->type = get_task_type(command);
    task->state = TASK_CREATED;

    //set burst time based on task type
    if (task->type == TASK_TYPE_SHELL) {
        task->total_burst_time = SHELL_COMMAND_BURST;
        task->remaining_burst_time = SHELL_COMMAND_BURST;
    } else {
        task->total_burst_time = extract_burst_time(command);
        task->remaining_burst_time = task->total_burst_time;
    }

    //initialize scheduling fields
    task->current_iteration = 0;
    task->round_number = 0;
    task->quantum = FIRST_ROUND_QUANTUM;

    //record arrival time
    gettimeofday(&task->arrival_time, NULL);
    memset(&task->start_time, 0, sizeof(struct timeval));
    memset(&task->end_time, 0, sizeof(struct timeval));

    //initialize output buffer
    task->output_buffer[0] = '\0';
    task->output_length = 0;

    return task;
}

//add task to waiting queue in thread-safe manner
int add_task_to_queue(Task* task) {
    pthread_mutex_lock(&waiting_queue.mutex);

    //check if queue is full
    if (waiting_queue.count >= MAX_TASKS) {
        pthread_mutex_unlock(&waiting_queue.mutex);
        return -1;
    }

    //reset start time only if system is completely idle
    pthread_mutex_lock(&scheduler_mutex);
    if (waiting_queue.count == 0 && schedule_summary.count == 0 && currently_running_task_id == -1) {
        gettimeofday(&schedule_summary.start_time, NULL);
    }
    pthread_mutex_unlock(&scheduler_mutex);

    //add task to queue and increment count
    waiting_queue.tasks[waiting_queue.count] = task;
    waiting_queue.count++;

    //signal scheduler that queue is not empty
    pthread_cond_signal(&waiting_queue.not_empty);
    pthread_mutex_unlock(&waiting_queue.mutex);
    return 0;
}

//remove specific task from queue by id
Task* remove_task_from_queue(int task_id) {
    pthread_mutex_lock(&waiting_queue.mutex);
    Task* removed_task = NULL;
    //search queue for matching task id
    for (int i = 0; i < waiting_queue.count; i++) {
        if (waiting_queue.tasks[i] != NULL && waiting_queue.tasks[i]->task_id == task_id) {
            removed_task = waiting_queue.tasks[i];
            //shift remaining tasks forward
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

//remove all tasks belonging to a specific client
void remove_client_tasks(int client_num) {
    pthread_mutex_lock(&waiting_queue.mutex);
    int i = 0;
    //iterate through queue removing matching client tasks
    while (i < waiting_queue.count) {
        if (waiting_queue.tasks[i] != NULL && waiting_queue.tasks[i]->client_num == client_num) {
            //free the task memory
            free(waiting_queue.tasks[i]);
            //shift remaining tasks forward
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


//select next task using hybrid rr+sjrf scheduling
Task* select_next_task() {
    if (waiting_queue.count == 0) return NULL;

    Task* selected = NULL;
    int selected_index = -1;

    //shell commands have absolute priority
    for (int i = 0; i < waiting_queue.count; i++) {
        Task* task = waiting_queue.tasks[i];
        if (task != NULL && task->remaining_burst_time == SHELL_COMMAND_BURST) {
            //avoid consecutive selection unless only task left
            if (task->task_id != waiting_queue.last_selected_id || waiting_queue.count == 1) {
                selected = task;
                selected_index = i;
                break;
            }
        }
    }

    //if no shell command, use shortest remaining time first
    if (selected == NULL) {
        int shortest_time = -1;

        for (int i = 0; i < waiting_queue.count; i++) {
            Task* task = waiting_queue.tasks[i];
            if (task == NULL) continue;

            //prevent same task from being selected consecutively
            if (task->task_id == waiting_queue.last_selected_id && waiting_queue.count > 1) {
                continue;
            }

            //select task with smallest remaining burst time
            if (shortest_time == -1 || task->remaining_burst_time < shortest_time) {
                shortest_time = task->remaining_burst_time;
                selected = task;
                selected_index = i;
            }
        }
    }

    //fallback to first task if nothing selected
    if (selected == NULL && waiting_queue.count > 0) {
        selected = waiting_queue.tasks[0];
        selected_index = 0;
    }

    //remove selected task from queue
    if (selected != NULL && selected_index >= 0) {
        for (int j = selected_index; j < waiting_queue.count - 1; j++) {
            waiting_queue.tasks[j] = waiting_queue.tasks[j + 1];
        }
        waiting_queue.tasks[waiting_queue.count - 1] = NULL;
        waiting_queue.count--;

        //update last selected to prevent consecutive selection
        waiting_queue.last_selected_id = selected->task_id;
    }

    return selected;
}


//log task state transition with color coding and remaining time
void log_task_state(Task* task, const char* state_msg) {
    char log_buffer[512];
    //select color based on state
    const char* color = COLOR_RESET;

    if (strcmp(state_msg, "created") == 0) color = COLOR_CYAN;
    else if (strcmp(state_msg, "started") == 0) color = COLOR_GREEN;
    else if (strcmp(state_msg, "waiting") == 0) color = COLOR_YELLOW;
    else if (strcmp(state_msg, "running") == 0) color = COLOR_MAGENTA;
    else if (strcmp(state_msg, "ended") == 0) color = COLOR_RED;

    pthread_mutex_lock(&scheduler_mutex);

    //shell commands show -1 for burst time, programs show remaining time
    if (task->remaining_burst_time == SHELL_COMMAND_BURST) {
        snprintf(log_buffer, sizeof(log_buffer), "[%d]--- %s%s%s (-1)",
                 task->client_num, color, state_msg, COLOR_RESET);
    } else {
        snprintf(log_buffer, sizeof(log_buffer), "[%d]--- %s%s%s (%d)",
                 task->client_num, color, state_msg, COLOR_RESET,
                 task->remaining_burst_time);
    }

    //output to stdout
    printf("%s\n", log_buffer);
    fflush(stdout);

    pthread_mutex_unlock(&scheduler_mutex);
}

//add entry to schedule summary after task execution
void add_schedule_entry(int task_id) {
    pthread_mutex_lock(&scheduler_mutex);
    //record task id and time when it executed
    if (schedule_summary.count < MAX_TASKS * 10) {
        schedule_summary.entries[schedule_summary.count].task_id = task_id;
        schedule_summary.entries[schedule_summary.count].completion_time = get_elapsed_seconds();
        schedule_summary.count++;
    }
    pthread_mutex_unlock(&scheduler_mutex);
}

//print final schedule summary in blue highlighting
void print_schedule_summary() {
    pthread_mutex_lock(&scheduler_mutex);

    //print schedule in format: P1-(3)-P2-(3)-...
    printf("\n%s", COLOR_BLUE);
    for (int i = 0; i < schedule_summary.count; i++) {
        if (i > 0) printf("-");
        printf("P%d-(%d)", schedule_summary.entries[i].task_id,
               schedule_summary.entries[i].completion_time);
    }
    printf("%s\n", COLOR_RESET);
    fflush(stdout);

    //reset summary for the next batch
    schedule_summary.count = 0;

    pthread_mutex_unlock(&scheduler_mutex);
}


//execute shell command and capture output
int execute_shell_command(Task* task) {
    int pipe_fd[2];
    //create pipe to capture child output
    if (pipe(pipe_fd) == -1) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]); close(pipe_fd[1]);
        return -1;
    } else if (pid == 0) {
        //child process
        close(pipe_fd[0]);
        //redirect stdout and stderr to pipe
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        //execute the command
        execlp("/bin/sh", "sh", "-c", task->command, NULL);
        exit(1);
    } else {
        //parent process
        close(pipe_fd[1]);
        //read output from child process
        ssize_t bytes = read(pipe_fd[0], task->output_buffer, sizeof(task->output_buffer) - 1);
        if (bytes > 0) {
            task->output_buffer[bytes] = '\0';
            task->output_length = bytes;
        } else {
            task->output_buffer[0] = '\0';
            task->output_length = 0;
        }
        close(pipe_fd[0]);
        //wait for child to complete
        waitpid(pid, NULL, 0);
    }
    return 0;
}

//execute program task with quantum time and preemption support
int execute_program_task(Task* task) {
    //determine quantum based on round number
    int quantum = (task->round_number == 0) ? FIRST_ROUND_QUANTUM : DEFAULT_QUANTUM;
    task->quantum = quantum;

    //run for quantum or remaining time, whichever is less
    int iterations_to_run = (task->remaining_burst_time < quantum) ?
                             task->remaining_burst_time : quantum;

    //execute task one iteration (second) at a time
    for (int i = 0; i < iterations_to_run; i++) {
        char line[128];
        //output progress to client
        snprintf(line, sizeof(line), "Demo %d/%d\n",
                 task->current_iteration + 1, task->total_burst_time);

        send(task->client_socket, line, strlen(line), 0);

        //simulate one second of work
        sleep(1);

        //update progress
        task->current_iteration++;
        task->remaining_burst_time--;

        //check if preemption is needed
        pthread_mutex_lock(&waiting_queue.mutex);
        int should_preempt = 0;

        //check for shell commands in queue
        for (int j = 0; j < waiting_queue.count; j++) {
            if (waiting_queue.tasks[j] != NULL &&
                waiting_queue.tasks[j]->remaining_burst_time == SHELL_COMMAND_BURST) {
                should_preempt = 1;
                break;
            }
        }

        //check for shorter job in queue (sjrf)
        for (int j = 0; j < waiting_queue.count && !should_preempt; j++) {
            if (waiting_queue.tasks[j] != NULL &&
                waiting_queue.tasks[j]->remaining_burst_time > 0 &&
                waiting_queue.tasks[j]->remaining_burst_time < task->remaining_burst_time) {
                should_preempt = 1;
                break;
            }
        }

        pthread_mutex_unlock(&waiting_queue.mutex);

        //return to queue if preempted
        if (should_preempt && task->remaining_burst_time > 0) {
            task->round_number++;
            return 0; //preempted
        }
    }

    task->round_number++;

    //check if task is done
    if (task->remaining_burst_time <= 0) {
        return 1; //completed
    }

    return 0; //yielding
}

//execute a single task
int execute_task(Task* task) {
    //record start time on first execution
    if (task->start_time.tv_sec == 0) {
        gettimeofday(&task->start_time, NULL);
    }

    //mark task as running
    task->state = TASK_RUNNING;
    currently_running_task_id = task->task_id;
    log_task_state(task, "running");

    int completed = 0;

    //execute based on task type
    if (task->type == TASK_TYPE_SHELL) {
        execute_shell_command(task);
        completed = 1;
    } else {
        completed = execute_program_task(task);
    }

    currently_running_task_id = -1;

    //handle task completion
    if (completed) {
        gettimeofday(&task->end_time, NULL);
        task->state = TASK_ENDED;
        log_task_state(task, "ended");

        //record in schedule summary for programs only
        if (task->type != TASK_TYPE_SHELL) {
            add_schedule_entry(task->task_id);
        }

        //send output back to client
        if (task->type == TASK_TYPE_SHELL) {
            if (task->output_length > 0) {
                send(task->client_socket, task->output_buffer, task->output_length, 0);
                log_bytes_sent(task->client_num, task->output_length);
            } else {
                send(task->client_socket, "\n", 1, 0);
                log_bytes_sent(task->client_num, 1);
            }
        } else {
            //estimate bytes sent for demo output
            int total_bytes = task->current_iteration * 12;
            log_bytes_sent(task->client_num, total_bytes);
        }

        //print summary when all tasks done
        pthread_mutex_lock(&waiting_queue.mutex);
        int queue_empty = (waiting_queue.count == 0);
        pthread_mutex_unlock(&waiting_queue.mutex);

        if (queue_empty && schedule_summary.count > 0) {
            print_schedule_summary();
        }

        return 1;
    } else {
        //task not complete, return to waiting
        task->state = TASK_WAITING;
        log_task_state(task, "waiting");

        //record in schedule summary
        if (task->type != TASK_TYPE_SHELL) {
            add_schedule_entry(task->task_id);
        }

        return 0;
    }
}

//main scheduler thread loop
void* scheduler_thread(void* arg) {
    (void)arg;

    //keep running until stopped
    while (scheduler_running) {
        pthread_mutex_lock(&waiting_queue.mutex);

        //wait for tasks to arrive if queue is empty
        while (waiting_queue.count == 0 && scheduler_running) {
            pthread_cond_wait(&waiting_queue.not_empty, &waiting_queue.mutex);
        }

        //check if should exit
        if (!scheduler_running) {
            pthread_mutex_unlock(&waiting_queue.mutex);
            break;
        }

        //select next task using scheduling algorithm
        Task* task = select_next_task();

        pthread_mutex_unlock(&waiting_queue.mutex);

        //execute the selected task
        if (task != NULL) {
            int completed = execute_task(task);

            //if task is done, free it; otherwise return to queue
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

//start scheduler in separate thread
void start_scheduler() {
    scheduler_running = 1;
    pthread_t tid;
    //create scheduler thread
    if (pthread_create(&tid, NULL, scheduler_thread, NULL) != 0) {
        perror("Failed to create scheduler thread");
        return;
    }
    //detach thread so it cleans up automatically
    pthread_detach(tid);
}

//stop scheduler gracefully
void stop_scheduler() {
    //signal scheduler to stop running
    scheduler_running = 0;
    //wake up scheduler thread if waiting
    pthread_mutex_lock(&waiting_queue.mutex);
    pthread_cond_signal(&waiting_queue.not_empty);
    pthread_mutex_unlock(&waiting_queue.mutex);

    //print final summary if there were any tasks
    if (schedule_summary.count > 0) {
        print_schedule_summary();
    }
}