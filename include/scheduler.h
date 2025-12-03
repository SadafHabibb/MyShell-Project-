//include/scheduler.h - Phase 4: Process Scheduler
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <pthread.h>
#include <sys/time.h>


#define MAX_TASKS 100              //maximum number of tasks in the waiting queue
#define FIRST_ROUND_QUANTUM 3      //quantum for first round (seconds)
#define DEFAULT_QUANTUM 7          //quantum for subsequent rounds (seconds)
#define SHELL_COMMAND_BURST -1     //special burst time for shell commands (immediate execution)
#define DEFAULT_BURST_TIME 10      //default burst time for unknown programs


typedef enum {
    TASK_CREATED,      //task has been created but not yet started
    TASK_WAITING,      //task is in waiting queue
    TASK_RUNNING,      //task is currently executing
    TASK_ENDED         //task has completed execution
} TaskState;


typedef enum {
    TASK_TYPE_SHELL,   //shell command (ls, pwd, etc.) - runs to completion immediately
    TASK_TYPE_PROGRAM  //program execution (demo N) - can be preempted
} TaskType;

typedef struct {
    int task_id;                   //unique task identifier
    int client_num;                //client number that submitted this task
    int client_socket;             //socket to send output back to client
    char command[4096];            //the command string to execute
    
    TaskType type;                 //shell command or program
    TaskState state;               //current state of the task
    
    int total_burst_time;          //total time needed for execution (N value for demo)
    int remaining_burst_time;      //remaining time to complete execution
    int current_iteration;         //current iteration (for demo program progress tracking)
    
    int round_number;              //which scheduling round the task is in
    int quantum;                   //current quantum allocated to this task
    
    struct timeval arrival_time;   //when the task was added to the queue
    struct timeval start_time;     //when the task first started running
    struct timeval end_time;       //when the task completed
    
    char output_buffer[4096];      //buffer to accumulate output
    int output_length;             //current length of output in buffer
} Task;


typedef struct {
    Task* tasks[MAX_TASKS];        //array of task pointers
    int count;                     //current number of tasks in queue
    int last_selected_id;          //ID of last selected task (to prevent consecutive selection)
    
    pthread_mutex_t mutex;         //mutex for thread-safe access
    pthread_cond_t not_empty;      //condition variable: signaled when queue becomes non-empty
    pthread_cond_t task_complete;  //condition variable: signaled when a task completes
} WaitingQueue;


typedef struct {
    int task_id;                   //task ID (Px)
    int completion_time;           //time at which task completed (relative to start)
} ScheduleEntry;

typedef struct {
    ScheduleEntry entries[MAX_TASKS * 10];  //scheduling order log
    int count;                              //number of entries
    struct timeval start_time;              //scheduler start time for relative calculations
} ScheduleSummary;


extern WaitingQueue waiting_queue;
extern ScheduleSummary schedule_summary;
extern pthread_mutex_t scheduler_mutex;
extern pthread_cond_t scheduler_cond;
extern int scheduler_running;
extern int currently_running_task_id;


/**
 * Initializes the waiting queue and all associated synchronization primitives
 * Must be called before any other scheduler functions
 */
void init_waiting_queue();

/**
 * Cleans up the waiting queue and frees all resources
 */
void destroy_waiting_queue();

/**
 * Creates a new task from a command string
 * Determines task type (shell vs program) and extracts burst time
 * 
 * @param command - the command string to create a task for
 * @param client_num - the client number submitting this command
 * @param client_socket - socket to send output back to client
 * @return pointer to newly created Task, or NULL on failure
 */
Task* create_task(const char* command, int client_num, int client_socket);

/**
 * Adds a task to the waiting queue
 * Thread-safe operation that signals the scheduler when queue becomes non-empty
 * 
 * @param task - pointer to the task to add
 * @return 0 on success, -1 if queue is full
 */
int add_task_to_queue(Task* task);

/**
 * Removes a specific task from the waiting queue
 * Used when a task completes or client disconnects
 * 
 * @param task_id - ID of the task to remove
 * @return pointer to removed task, or NULL if not found
 */
Task* remove_task_from_queue(int task_id);

/**
 * Removes all tasks belonging to a specific client
 * Called when a client disconnects
 * 
 * @param client_num - client number whose tasks should be removed
 */
void remove_client_tasks(int client_num);

/**
 * Selects the next task to execute using the combined RR + SJRF algorithm
 * Selection criteria:
 * 1. Shell commands (burst_time == -1) have highest priority
 * 2. Among programs, select shortest remaining job first
 * 3. If remaining times are equal, use FCFS (first in queue)
 * 4. Same task cannot be selected twice in a row unless it's the only task
 * 
 * @return pointer to selected task, or NULL if queue is empty
 */
Task* select_next_task();

/**
 * Executes a task for one quantum or until completion
 * For shell commands: executes to completion in one round
 * For programs: executes for quantum seconds, then returns to queue if not done
 * 
 * @param task - pointer to the task to execute
 * @return 1 if task completed, 0 if task needs more time
 */
int execute_task(Task* task);

/**
 * Main scheduler thread function
 * Continuously selects and executes tasks from the waiting queue
 * Implements the combined RR + SJRF scheduling algorithm
 * 
 * @param arg - unused
 * @return NULL
 */
void* scheduler_thread(void* arg);

/**
 * Starts the scheduler thread
 * Creates a detached thread running the scheduler loop
 */
void start_scheduler();

/**
 * Stops the scheduler thread
 * Sets running flag to 0 and signals condition variable
 */
void stop_scheduler();

/**
 * Logs a task state change with formatted output
 * Uses the logging format specified in project requirements
 * 
 * @param task - the task that changed state
 * @param state_msg - message describing the state (created, started, waiting, running, ended)
 */
void log_task_state(Task* task, const char* state_msg);

/**
 * Adds an entry to the scheduling summary
 * Used to build the execution order log displayed at the end
 * 
 * @param task_id - ID of the task that was scheduled
 */
void add_schedule_entry(int task_id);

/**
 * Prints the scheduling summary showing execution order
 * Format: P5-(3)-P7-(6)-P6-(13)-P7-(20)-P6-(22)-P7-(24)
 */
void print_schedule_summary();

/**
 * Determines if a command is a shell command or a program
 * Shell commands: ls, pwd, cd, echo, cat, mkdir, rm, etc.
 * Programs: ./demo, ./program, or any command with burst time argument
 * 
 * @param command - the command string to check
 * @return TASK_TYPE_SHELL or TASK_TYPE_PROGRAM
 */
TaskType get_task_type(const char* command);

/**
 * Extracts the burst time (N value) from a demo command
 * Example: "./demo 12" returns 12
 * 
 * @param command - the command string
 * @return burst time value, or DEFAULT_BURST_TIME if not found
 */
int extract_burst_time(const char* command);

/**
 * Gets current time in seconds since scheduler started
 * Used for scheduling summary timestamps
 * 
 * @return seconds elapsed since scheduler start
 */
int get_elapsed_seconds();

#endif //SCHEDULER_H