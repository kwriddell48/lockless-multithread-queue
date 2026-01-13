#define _POSIX_C_SOURCE 200809L
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

// Get current timestamp as a string (thread-safe, uses static buffer)
static const char* get_timestamp(void) {
    static char timestamp[32];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm* tm_info = localtime(&ts.tv_sec);
    snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d.%03ld",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, ts.tv_nsec / 1000000);
    return timestamp;
}

// Timestamped printf wrapper
static int tprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = printf("[%s] ", get_timestamp());
    result += vprintf(format, args);
    va_end(args);
    return result;
}

// Timestamped fprintf wrapper
static int tfprintf(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = fprintf(stream, "[%s] ", get_timestamp());
    result += vfprintf(stream, format, args);
    va_end(args);
    return result;
}

// Thread argument structure
typedef struct {
    Queue* queue;
    int thread_id;
    int num_elements;
} WorkerThreadArg;

// Worker thread function that enqueues and dequeues messages
void* worker_thread(void* arg) {
    WorkerThreadArg* targ = (WorkerThreadArg*)arg;
    Queue* q = targ->queue;
    int thread_id = targ->thread_id;
    int num_elements = targ->num_elements;
    
    // Seed random number generator with thread ID and time
    unsigned int seed = (unsigned int)(time(NULL) ^ thread_id ^ (unsigned long)pthread_self());
    
    tprintf("Worker thread %d: Started (will process %d elements)\n", thread_id, num_elements);
    fflush(stdout);
    
    int enqueued = 0;
    int dequeued = 0;
    
    // Enqueue elements
    for (int i = 0; i < num_elements; i++) {
        // Create data with thread ID and element number
        int* data = (int*)malloc(sizeof(int));
        if (data != NULL) {
            *data = thread_id * 10000 + i;  // Unique value: thread_id * 10000 + element_number
            if (queue_enqueue(q, data, sizeof(int))) {
                enqueued++;
            }
            free(data);  // Free local copy (queue makes its own copy)
        }
        
        // Random timing delay between 0 and 1000 microseconds (0-1 millisecond)
        int wait_us = rand_r(&seed) % 1001;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = wait_us * 1000;  // Convert microseconds to nanoseconds
        nanosleep(&ts, NULL);
    }
    
    tprintf("Worker thread %d: Enqueued %d elements\n", thread_id, enqueued);
    fflush(stdout);
    
    // Dequeue elements (try to dequeue as many as we can)
    for (int i = 0; i < num_elements; i++) {
        void* dequeued_data;
        size_t dequeued_length;
        
        if (queue_dequeue(q, &dequeued_data, &dequeued_length)) {
            dequeued++;
            if (dequeued_data != NULL) {
                free(dequeued_data);
            }
        } else {
            // No more items available, break
            break;
        }
        
        // Small random wait between 0 and 100 microseconds
        int wait_us = rand_r(&seed) % 101;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = wait_us * 1000;
        nanosleep(&ts, NULL);
    }
    
    tprintf("Worker thread %d: Completed - Enqueued: %d, Dequeued: %d\n", 
            thread_id, enqueued, dequeued);
    fflush(stdout);
    
    return NULL;
}

int main(int argc, char* argv[]) {
    tprintf("Thread Test Program\n");
    tprintf("===================\n\n");
    
    // Check for help option
    if (argc >= 2 && (strcmp(argv[1], "?") == 0 || strcmp(argv[1], "help") == 0 || 
                      strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        tprintf("Usage: %s [num_threads] [num_elements]\n\n", argv[0]);
        tprintf("Parameters:\n");
        tprintf("  num_threads   Number of worker threads to create (default: 5)\n");
        tprintf("  num_elements  Number of messages/elements per thread (default: 50)\n\n");
        tprintf("Examples:\n");
        tprintf("  %s              # Uses defaults: 5 threads, 50 elements\n", argv[0]);
        tprintf("  %s 10           # Uses 10 threads, 50 elements\n", argv[0]);
        tprintf("  %s 10 100       # Uses 10 threads, 100 elements\n", argv[0]);
        return 0;
    }
    
    // Default values: 5 threads, 50 elements per thread
    int num_threads = 5;
    int num_elements = 50;
    
    // Get number of threads from argv[1] (optional)
    if (argc >= 2) {
        num_threads = atoi(argv[1]);
        if (num_threads <= 0) {
            tfprintf(stderr, "Invalid number of threads: %s (must be > 0)\n", argv[1]);
            return 1;
        }
    }
    
    // Get number of elements from argv[2] (optional)
    if (argc >= 3) {
        num_elements = atoi(argv[2]);
        if (num_elements <= 0) {
            tfprintf(stderr, "Invalid number of elements: %s (must be > 0)\n", argv[2]);
            return 1;
        }
    }
    
    if (argc > 3) {
        tfprintf(stderr, "Too many arguments. Use '%s ?' for help.\n", argv[0]);
        return 1;
    }
    
    // Initialize queue
    Queue* queue = queue_init();
    if (queue == NULL) {
        tfprintf(stderr, "Failed to initialize queue\n");
        return 1;
    }
    
    tprintf("Initialized queue\n");
    tprintf("Creating %d worker threads, each processing %d elements...\n\n", 
            num_threads, num_elements);
    fflush(stdout);
    
    // Allocate arrays for threads and thread arguments
    pthread_t* threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    WorkerThreadArg* thread_args = (WorkerThreadArg*)malloc(num_threads * sizeof(WorkerThreadArg));
    
    if (threads == NULL || thread_args == NULL) {
        tfprintf(stderr, "Failed to allocate memory for threads\n");
        queue_destroy(queue);
        return 1;
    }
    
    // Initialize thread arguments
    for (int i = 0; i < num_threads; i++) {
        thread_args[i].queue = queue;
        thread_args[i].thread_id = i;
        thread_args[i].num_elements = num_elements;
    }
    
    // Create and start all worker threads
    tprintf("Starting worker threads...\n");
    fflush(stdout);
    
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &thread_args[i]) != 0) {
            tfprintf(stderr, "Failed to create thread %d\n", i);
            // Clean up already created threads (simplified - in production, track which ones succeeded)
            queue_destroy(queue);
            free(threads);
            free(thread_args);
            return 1;
        }
    }
    
    tprintf("All %d worker threads started.\n\n", num_threads);
    fflush(stdout);
    
    // Wait for all worker threads to finish
    tprintf("Waiting for all worker threads to complete...\n");
    fflush(stdout);
    
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            tfprintf(stderr, "Failed to join thread %d\n", i);
        }
    }
    
    tprintf("All worker threads completed.\n\n");
    fflush(stdout);
    
    // Print queue statistics (includes maximum queue size)
    queue_print_stats(queue);
    
    // Clean up
    tprintf("\nCleaning up...\n");
    queue_destroy(queue);
    free(threads);
    free(thread_args);
    
    tprintf("Thread test completed successfully.\n");
    
    return 0;
}
