// demo.c - Phase 4: Test Program for Scheduler Testing
// This program simulates a process that runs for N seconds
// It prints one output line per second: "Demo X/N"
// Usage: ./demo N  (where N is the number of seconds to run)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    // check command line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s N\n", argv[0]);
        fprintf(stderr, "  N = number of seconds to run\n");
        return 1;
    }
    
    // parse the N value
    int n = atoi(argv[1]);
    
    if (n <= 0) {
        fprintf(stderr, "Error: N must be a positive integer\n");
        return 1;
    }
    
    // run for N seconds, printing one line per second
    // this simulates a process with burst time of N
    for (int i = 0; i < n; i++) {
        // print the current iteration
        // format: "Demo X/N" where X is current (0-indexed), N is total
        printf("Demo %d/%d\n", i, n);
        fflush(stdout);  // important: flush immediately so scheduler can see output
        
        // sleep for 1 second (simulates 1 unit of work)
        sleep(1);
    }
    
    return 0;
}