#define _POSIX_C_SOURCE 200809L
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

// Print function for integers
void print_int(const void* data, size_t length) {
    if (data != NULL && length == sizeof(int)) {
        printf("%d", *(const int*)data);
    } else {
        printf("(invalid int)");
    }
}

// Print function for strings
void print_string(const void* data, size_t length) {
    if (data != NULL && length > 0) {
        printf("\"%.*s\"", (int)length, (const char*)data);
    } else {
        printf("(empty)");
    }
}

int main(void) {
    printf("Lock-Free Doubly Linked List Queue Demo\n");
    printf("========================================\n\n");
    
    // Initialize queue
    Queue* queue = queue_init();
    if (queue == NULL) {
        fprintf(stderr, "Failed to initialize queue\n");
        return 1;
    }
    
    printf("Initial queue state:\n");
    queue_print(queue, NULL);
    printf("Is empty: %s\n\n", queue_is_empty(queue) ? "Yes" : "No");
    
    // Enqueue integer elements
    printf("Enqueuing integer elements: 10, 20, 30, 40, 50\n");
    int values[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        assert(queue_enqueue(queue, &values[i], sizeof(int)) == true);
    }
    
    queue_print(queue, print_int);
    printf("Size: %zu\n", queue_size(queue));
    printf("Is empty: %s\n\n", queue_is_empty(queue) ? "Yes" : "No");
    
    // Dequeue integer elements
    printf("Dequeuing integer elements:\n");
    void* dequeued_data;
    size_t dequeued_length;
    while (!queue_is_empty(queue)) {
        if (queue_dequeue(queue, &dequeued_data, &dequeued_length)) {
            if (dequeued_data != NULL && dequeued_length == sizeof(int)) {
                int value = *(int*)dequeued_data;
                printf("  Dequeued: %d (length: %zu bytes)\n", value, dequeued_length);
                free(dequeued_data);  // Free the data returned by dequeue
            }
            queue_print(queue, print_int);
            printf("  Size: %zu\n\n", queue_size(queue));
        }
    }
    
    // Test empty queue dequeue
    printf("Attempting to dequeue from empty queue:\n");
    bool result = queue_dequeue(queue, &dequeued_data, &dequeued_length);
    printf("  Result: %s (expected: false)\n\n", result ? "Success" : "Failed (as expected)");
    
    // Enqueue string elements
    printf("Enqueuing string elements:\n");
    const char* strings[] = {"Hello", "World", "Queue", "Test"};
    for (int i = 0; i < 4; i++) {
        size_t str_len = strlen(strings[i]) + 1;  // Include null terminator
        assert(queue_enqueue(queue, strings[i], str_len) == true);
        printf("  Enqueued: \"%s\" (length: %zu bytes)\n", strings[i], str_len);
    }
    
    printf("\nQueue contents:\n");
    queue_print(queue, print_string);
    printf("Size: %zu\n\n", queue_size(queue));
    
    // Dequeue one string
    printf("Dequeuing one string element:\n");
    if (queue_dequeue(queue, &dequeued_data, &dequeued_length)) {
        if (dequeued_data != NULL) {
            printf("  Dequeued: \"%s\" (length: %zu bytes)\n", (char*)dequeued_data, dequeued_length);
            free(dequeued_data);  // Free the data
        }
        queue_print(queue, print_string);
        printf("  Size: %zu\n\n", queue_size(queue));
    }
    
    // Enqueue mixed data types
    printf("Enqueuing mixed data: integer and string\n");
    int num = 42;
    queue_enqueue(queue, &num, sizeof(int));
    const char* msg = "Mixed";
    queue_enqueue(queue, msg, strlen(msg) + 1);
    
    printf("\nQueue with mixed data types:\n");
    queue_print(queue, NULL);  // NULL print function shows addresses
    
    // Dequeue all remaining elements
    printf("\nDequeuing all remaining elements:\n");
    while (!queue_is_empty(queue)) {
        if (queue_dequeue(queue, &dequeued_data, &dequeued_length)) {
            printf("  Dequeued: ptr=%p, length=%zu bytes\n", dequeued_data, dequeued_length);
            free(dequeued_data);
        }
    }
    
    // Clean up
    queue_destroy(queue);
    printf("\nQueue destroyed successfully.\n");
    
    // Multi-threaded test
    printf("\n");
    printf("========================================\n");
    printf("Multi-Threaded Test\n");
    printf("========================================\n\n");
    
    Queue* test_queue = queue_init();
    if (test_queue == NULL) {
        fprintf(stderr, "Failed to initialize test queue\n");
        return 1;
    }
    
    // Thread argument structure
    typedef struct {
        Queue* queue;
        int thread_id;
    } ThreadArg;
    
    // Thread function that enqueues 100 items, then dequeues them with random waits
    void* enqueue_thread(void* arg) {
        ThreadArg* targ = (ThreadArg*)arg;
        Queue* q = targ->queue;
        int thread_id = targ->thread_id;
        
        // Seed random number generator with thread ID and time
        unsigned int seed = (unsigned int)(time(NULL) ^ thread_id ^ (unsigned long)pthread_self());
        
        // Phase 1: Enqueue 100 items
        for (int i = 0; i < 100; i++) {
            // Create data with thread ID and item number
            int* data = (int*)malloc(sizeof(int));
            if (data != NULL) {
                *data = thread_id * 1000 + i;  // Unique value: thread_id * 1000 + item_number
                queue_enqueue(q, data, sizeof(int));
                free(data);  // Free local copy (queue makes its own copy)
            }
            
            // Random wait between 0 and 1000 microseconds
            int wait_us = rand_r(&seed) % 1001;
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = wait_us * 1000;  // Convert microseconds to nanoseconds
            nanosleep(&ts, NULL);
        }
        
        // Phase 2: Dequeue items (try to dequeue 100 items)
        for (int i = 0; i < 100; i++) {
            void* dequeued_data;
            size_t dequeued_length;
            
            // Try to dequeue an item
            if (queue_dequeue(q, &dequeued_data, &dequeued_length)) {
                // Successfully dequeued - free the data
                if (dequeued_data != NULL) {
                    free(dequeued_data);
                }
            }
            
            // Random wait between 0 and 1000 microseconds
            int wait_us = rand_r(&seed) % 1001;
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = wait_us * 1000;  // Convert microseconds to nanoseconds
            nanosleep(&ts, NULL);
        }
        
        return NULL;
    }
    
    // Create 10 threads
    const int num_threads = 10;
    pthread_t threads[num_threads];
    ThreadArg thread_args[num_threads];
    
    printf("Starting %d threads, each adding 100 items then deleting 100 items with random wait times...\n", num_threads);
    
    // Create threads
    for (int i = 0; i < num_threads; i++) {
        thread_args[i].queue = test_queue;
        thread_args[i].thread_id = i;
        if (pthread_create(&threads[i], NULL, enqueue_thread, &thread_args[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            queue_destroy(test_queue);
            return 1;
        }
    }
    
    // Wait for all threads to complete
    printf("Waiting for all threads to complete...\n");
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Failed to join thread %d\n", i);
        }
    }
    
    printf("All threads completed.\n\n");
    
    // Print stats
    queue_print_stats(test_queue);
    
    // Clean up
    printf("\nCleaning up test queue...\n");
    queue_destroy(test_queue);
    printf("Test queue destroyed successfully.\n");
    
    return 0;
}
