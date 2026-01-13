#define _POSIX_C_SOURCE 200809L
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

// Default timeout for mutex locks and condition waits (30 seconds)
#define DEFAULT_MUTEX_TIMEOUT_SEC 30

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

// Print function for integers
void print_int(const void* data, size_t length) {
    if (data != NULL && length == sizeof(int)) {
        tprintf("%d", *(const int*)data);
    } else {
        tprintf("(invalid int)");
    }
}

// Print function for strings
void print_string(const void* data, size_t length) {
    if (data != NULL && length > 0) {
        tprintf("\"%.*s\"", (int)length, (const char*)data);
    } else {
        tprintf("(empty)");
    }
}

int main(int argc, char* argv[]) {
    tprintf("Lock-Free Doubly Linked List Queue Demo\n");
    tprintf("========================================\n\n");
    
    // Initialize queue
    Queue* queue = queue_init();
    if (queue == NULL) {
        tfprintf(stderr, "Failed to initialize queue\n");
        return 1;
    }
    
    tprintf("Initial queue state:\n");
    queue_print(queue, NULL);
    tprintf("Is empty: %s\n\n", queue_is_empty(queue) ? "Yes" : "No");
    
    // Enqueue integer elements
    tprintf("Enqueuing integer elements: 10, 20, 30, 40, 50\n");
    int values[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        assert(queue_enqueue(queue, &values[i], sizeof(int)) == true);
    }
    
    queue_print(queue, print_int);
    tprintf("Size: %zu\n", queue_size(queue));
    tprintf("Is empty: %s\n\n", queue_is_empty(queue) ? "Yes" : "No");
    
    // Dequeue integer elements
    tprintf("Dequeuing integer elements:\n");
    void* dequeued_data;
    size_t dequeued_length;
    while (!queue_is_empty(queue)) {
        if (queue_dequeue(queue, &dequeued_data, &dequeued_length)) {
            if (dequeued_data != NULL && dequeued_length == sizeof(int)) {
                int value = *(int*)dequeued_data;
                tprintf("  Dequeued: %d (length: %zu bytes)\n", value, dequeued_length);
                free(dequeued_data);  // Free the data returned by dequeue
            }
            queue_print(queue, print_int);
            tprintf("  Size: %zu\n\n", queue_size(queue));
        }
    }
    
    // Test empty queue dequeue
    tprintf("Attempting to dequeue from empty queue:\n");
    bool result = queue_dequeue(queue, &dequeued_data, &dequeued_length);
    tprintf("  Result: %s (expected: false)\n\n", result ? "Success" : "Failed (as expected)");
    
    // Enqueue string elements
    tprintf("Enqueuing string elements:\n");
    const char* strings[] = {"Hello", "World", "Queue", "Test"};
    for (int i = 0; i < 4; i++) {
        size_t str_len = strlen(strings[i]) + 1;  // Include null terminator
        assert(queue_enqueue(queue, strings[i], str_len) == true);
        tprintf("  Enqueued: \"%s\" (length: %zu bytes)\n", strings[i], str_len);
    }
    
    tprintf("\nQueue contents:\n");
    queue_print(queue, print_string);
    tprintf("Size: %zu\n\n", queue_size(queue));
    
    // Dequeue one string
    tprintf("Dequeuing one string element:\n");
    if (queue_dequeue(queue, &dequeued_data, &dequeued_length)) {
        if (dequeued_data != NULL) {
            tprintf("  Dequeued: \"%s\" (length: %zu bytes)\n", (char*)dequeued_data, dequeued_length);
            free(dequeued_data);  // Free the data
        }
        queue_print(queue, print_string);
        tprintf("  Size: %zu\n\n", queue_size(queue));
    }
    
    // Enqueue mixed data types
    tprintf("Enqueuing mixed data: integer and string\n");
    int num = 42;
    queue_enqueue(queue, &num, sizeof(int));
    const char* msg = "Mixed";
    queue_enqueue(queue, msg, strlen(msg) + 1);
    
    tprintf("\nQueue with mixed data types:\n");
    queue_print(queue, NULL);  // NULL print function shows addresses
    
    // Dequeue all remaining elements
    tprintf("\nDequeuing all remaining elements:\n");
    while (!queue_is_empty(queue)) {
        if (queue_dequeue(queue, &dequeued_data, &dequeued_length)) {
            tprintf("  Dequeued: ptr=%p, length=%zu bytes\n", dequeued_data, dequeued_length);
            free(dequeued_data);
        }
    }
    
    // Clean up
    queue_destroy(queue);
    tprintf("\nQueue destroyed successfully.\n");
    
    // Multi-threaded test
    tprintf("\n");
    tprintf("========================================\n");
    tprintf("Multi-Threaded Test\n");
    tprintf("========================================\n\n");
    
    Queue* test_queue = queue_init();
    if (test_queue == NULL) {
        tfprintf(stderr, "Failed to initialize test queue\n");
        return 1;
    }
    
    // Thread argument structure
    typedef struct {
        Queue* queue;
        int thread_id;
        int items_per_thread;
        int mutex_timeout_sec;
        pthread_mutex_t* start_mutex;
        pthread_cond_t* start_cond;
        int* threads_ready;
        int* threads_started;
        pthread_mutex_t* nq_done_mutex;
        pthread_cond_t* nq_done_cond;
        int* threads_nq_done;
        int* total_threads;
    } ThreadArg;
    
    // Thread function that enqueues items, then dequeues them with random waits
    // (NQ thread - waits until all threads are started before proceeding)
    void* enqueue_thread(void* arg) {
        ThreadArg* targ = (ThreadArg*)arg;
        Queue* q = targ->queue;
        int thread_id = targ->thread_id;
        int items_per_thread = targ->items_per_thread;
        
        // Seed random number generator with thread ID and time
        unsigned int seed = (unsigned int)(time(NULL) ^ thread_id ^ (unsigned long)pthread_self());
        
        // Print thread start message
        tprintf("Thread %d: Started\n", thread_id);
        fflush(stdout);
        
        // Counters for NQ and DQ operations
        int nq_count = 0;
        int dq_count = 0;
        
        // Wait until all threads are started - signal that this thread is ready
        // Use regular mutex lock (should be available immediately)
        pthread_mutex_lock(targ->start_mutex);
        
        (*targ->threads_ready)++;
        pthread_mutex_unlock(targ->start_mutex);
        
        // Wait until main thread signals that all threads have been started (with timeout)
        struct timespec timeout;
        while (*targ->threads_ready < *targ->threads_started) {
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += targ->mutex_timeout_sec * 2;
            int wait_result_start = pthread_cond_timedwait(targ->start_cond, targ->start_mutex, &timeout);
            if (wait_result_start == ETIMEDOUT) {
                tfprintf(stderr, "Thread %d: Timeout waiting for all threads to start - %d/%d\n", 
                        thread_id, *targ->threads_ready, *targ->threads_started);
                // Fall through to retry the loop
            }
        }
        
        // Phase 1: Enqueue items (NQ)
        for (int i = 0; i < items_per_thread; i++) {
            // Create data with thread ID and item number
            int* data = (int*)malloc(sizeof(int));
            if (data != NULL) {
                *data = thread_id * 1000 + i;  // Unique value: thread_id * 1000 + item_number
                if (queue_enqueue(q, data, sizeof(int))) {
                    nq_count++;
                }
                free(data);  // Free local copy (queue makes its own copy)
            }
            
            // Random wait between 0 and 1000 microseconds
            int wait_us = rand_r(&seed) % 1001;
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = wait_us * 1000;  // Convert microseconds to nanoseconds
            nanosleep(&ts, NULL);
        }
        
        // Print message that NQ phase is complete
        tprintf("Thread %d: Done with NQ phase (enqueued %d items)\n", thread_id, nq_count);
        fflush(stdout);
        
        // Wait for all threads to finish NQ before starting DQ
        // Use regular mutex lock (should be available immediately)
        pthread_mutex_lock(targ->nq_done_mutex);
    
        (*targ->threads_nq_done)++;
        pthread_mutex_unlock(targ->nq_done_mutex);
        int total_needed = *targ->total_threads;
        
        // Wait until all threads have finished NQ (with timeout)
        // Always check condition in a loop to handle missed broadcasts
        // Note: timeout variable already declared in start barrier section above
        while (*targ->threads_nq_done < total_needed) {
            // If we're the last thread, broadcast before waiting
            if (*targ->threads_nq_done >= total_needed) {
                pthread_cond_broadcast(targ->nq_done_cond);
                break;
            }
            
            // Calculate timeout for this wait
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += targ->mutex_timeout_sec*2000;
            
            // Wait on condition variable (releases mutex, waits, reacquires mutex)
            int wait_result_nq = pthread_cond_timedwait(targ->nq_done_cond, targ->nq_done_mutex, &timeout);
            
            // Ensure we broadcast if we're the last one (in case we broke out of loop)
            if (*targ->threads_nq_done >= total_needed) {
                pthread_cond_broadcast(targ->nq_done_cond);
            }
            
            if (wait_result_nq == ETIMEDOUT) {
                // Check again after timeout - maybe all threads finished while we were waiting
                if (*targ->threads_nq_done >= total_needed) {
                    // All threads finished, break out of loop
                    break;
                }
                tfprintf(stderr, "Thread %d: Timeout waiting for all threads to finish NQ (error: %d, done: %d/%d)\n", 
                        thread_id, wait_result_nq, *targ->threads_nq_done, total_needed);
                // Fall through to retry the loop (recheck condition)
            }
            // If woken by broadcast or spurious wakeup, loop will recheck condition
        }
        
       
        
        // Print message that DQ phase is starting
        tprintf("Thread %d: Starting DQ phase (dequeue process)\n", thread_id);
        fflush(stdout);
        
        // Phase 2: Dequeue items (DQ) - continue until no more nodes available
        while (true) {
            void* dequeued_data;
            size_t dequeued_length;
            
            // Try to dequeue an item
            if (queue_dequeue(q, &dequeued_data, &dequeued_length)) {
                // Successfully dequeued - free the data
                dq_count++;
                if (dequeued_data != NULL) {
                    free(dequeued_data);
                }
            } else {
                // No more items to dequeue, exit loop
                break;
            }
            
            // Random wait between 0 and 1000 microseconds
            int wait_us = rand_r(&seed) % 1001;
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = wait_us * 1000;  // Convert microseconds to nanoseconds
            nanosleep(&ts, NULL);
        }
        
        // Print thread completion message with statistics
        tprintf("Thread %d: Completed - NQs: %d, DQs: %d\n", thread_id, nq_count, dq_count);
        fflush(stdout);
        
        return NULL;
    }
    
    // Check for help option
    if (argc >= 2 && (strcmp(argv[1], "?") == 0 || strcmp(argv[1], "help") == 0 || 
                      strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        tprintf("Usage: %s [num_threads] [items_per_thread] [mutex_timeout_sec]\n\n", argv[0]);
        tprintf("Optional parameters:\n");
        tprintf("  num_threads      Number of threads to create (default: 10)\n");
        tprintf("  items_per_thread Number of messages (nodes) to loop per queue (default: 100)\n");
        tprintf("  mutex_timeout_sec Mutex timeout in seconds (default: 30)\n\n");
        tprintf("Examples:\n");
        tprintf("  %s              # Uses defaults: 10 threads, 100 items, 30 sec timeout\n", argv[0]);
        tprintf("  %s 5            # Uses 5 threads, 100 items, 30 sec timeout\n", argv[0]);
        tprintf("  %s 5 200        # Uses 5 threads, 200 items, 30 sec timeout\n", argv[0]);
        tprintf("  %s 5 200 60     # Uses 5 threads, 200 items, 60 sec timeout\n", argv[0]);
        queue_destroy(test_queue);
        return 0;
    }
    
    // Default values: 10 threads, 100 items per thread, 30 seconds timeout
    int num_threads = 10;
    int items_per_thread = 100;
    int mutex_timeout_sec = DEFAULT_MUTEX_TIMEOUT_SEC;
    
    // Get number of threads from argv[1] (optional)
    if (argc >= 2) {
        num_threads = atoi(argv[1]);
        if (num_threads <= 0) {
            tfprintf(stderr, "Invalid number of threads: %s (must be > 0)\n", argv[1]);
            queue_destroy(test_queue);
            return 1;
        }
    }
    
    // Get number of items per thread from argv[2] (optional)
    if (argc >= 3) {
        items_per_thread = atoi(argv[2]);
        if (items_per_thread <= 0) {
            tfprintf(stderr, "Invalid number of items per thread: %s (must be > 0)\n", argv[2]);
            queue_destroy(test_queue);
            return 1;
        }
    }
    
    // Get mutex timeout from argv[3] (optional)
    if (argc >= 4) {
        mutex_timeout_sec = atoi(argv[3]);
        if (mutex_timeout_sec <= 0) {
            tfprintf(stderr, "Invalid mutex timeout: %s (must be > 0)\n", argv[3]);
            queue_destroy(test_queue);
            return 1;
        }
    }
    
    if (argc > 4) {
        tfprintf(stderr, "Too many arguments. Use '%s ?' for help.\n", argv[0]);
        queue_destroy(test_queue);
        return 1;
    }
    
    pthread_t threads[num_threads];
    ThreadArg thread_args[num_threads];
    
    // Synchronization for thread startup barrier
    pthread_mutex_t start_mutex;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&start_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    
    pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
    int threads_ready = 0;
    int threads_started = num_threads;
    
    // Synchronization for NQ completion barrier
    pthread_mutex_t nq_done_mutex;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&nq_done_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    
    pthread_cond_t nq_done_cond = PTHREAD_COND_INITIALIZER;
    int threads_nq_done = 0;
    int total_threads = num_threads;
    
    tprintf("\nStarting %d threads, each adding %d items then deleting %d items with random wait times...\n", num_threads, items_per_thread, items_per_thread);
    fflush(stdout);
    
    // Initialize thread arguments
    for (int i = 0; i < num_threads; i++) {
        thread_args[i].queue = test_queue;
        thread_args[i].thread_id = i;
        thread_args[i].items_per_thread = items_per_thread;
        thread_args[i].start_mutex = &start_mutex;
        thread_args[i].start_cond = &start_cond;
        thread_args[i].threads_ready = &threads_ready;
        thread_args[i].threads_started = &threads_started;
        thread_args[i].nq_done_mutex = &nq_done_mutex;
        thread_args[i].nq_done_cond = &nq_done_cond;
        thread_args[i].threads_nq_done = &threads_nq_done;
        thread_args[i].total_threads = &total_threads;
    }
    
    // Create threads (they will wait until all are started)
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, enqueue_thread, &thread_args[i]) != 0) {
            tfprintf(stderr, "Failed to create thread %d\n", i);
            queue_destroy(test_queue);
            return 1;
        }
    }
    
    // Wait for all threads to be ready (waiting on condition variable)
    // Use regular mutex lock (should be available immediately)
    pthread_mutex_lock(&start_mutex);
    
    while (threads_ready < num_threads) {
        pthread_mutex_unlock(&start_mutex);
        // Small sleep to avoid busy-waiting
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000;  // 1 millisecond
        nanosleep(&ts, NULL);
        
        // Re-acquire mutex (should be available)
        pthread_mutex_lock(&start_mutex);
    }
    // All threads are ready, signal them all to proceed
    pthread_cond_broadcast(&start_cond);
    pthread_mutex_unlock(&start_mutex);
    
    tprintf("All threads started and signaled to begin work.\n");
    fflush(stdout);
    
    // Wait for all threads to complete
    tprintf("Waiting for all threads to complete...\n");
    fflush(stdout);
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            tfprintf(stderr, "Failed to join thread %d\n", i);
        }
    }
    
    tprintf("All threads completed.\n\n");
    fflush(stdout);
    
    // Print stats
    queue_print_stats(test_queue);
    
    // Clean up
    tprintf("\nCleaning up test queue...\n");
    queue_destroy(test_queue);
    
    // Destroy mutexes
    pthread_mutex_destroy(&start_mutex);
    pthread_mutex_destroy(&nq_done_mutex);
    
    tprintf("Test queue destroyed successfully.\n");
    
    return 0;
}
