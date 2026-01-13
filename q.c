#include <stdio.h>
#include <stdlib.h>
#include "queue.h"
#include <time.h>
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

int main(void) {
    tprintf("Simple Queue Test\n");
    tprintf("=================\n\n");
    fflush(stdout);
    
    // Initialize queue
    tprintf("1. Initializing queue...\n");
    fflush(stdout);
    Queue* q = queue_init();
    if (q == NULL) {
        tprintf("ERROR: queue_init failed\n");
        return 1;
    }
    tprintf("   Queue initialized successfully\n\n");
    fflush(stdout);
    
    // Test enqueue
    tprintf("2. Testing enqueue...\n");
    fflush(stdout);
    int value1 = 10;
    int value2 = 20;
    int value3 = 30;
    
    if (queue_enqueue(q, &value1, sizeof(int))) {
        tprintf("   Enqueued: %d\n", value1);
    } else {
        tprintf("   ERROR: Failed to enqueue %d\n", value1);
    }
    fflush(stdout);
    
    if (queue_enqueue(q, &value2, sizeof(int))) {
        tprintf("   Enqueued: %d\n", value2);
    } else {
        tprintf("   ERROR: Failed to enqueue %d\n", value2);
    }
    fflush(stdout);
    
    if (queue_enqueue(q, &value3, sizeof(int))) {
        tprintf("   Enqueued: %d\n", value3);
    } else {
        tprintf("   ERROR: Failed to enqueue %d\n", value3);
    }
    fflush(stdout);
    
    tprintf("   Queue size: %zu\n\n", queue_size(q));
    fflush(stdout);
    
    // Test dequeue
    tprintf("3. Testing dequeue...\n");
    fflush(stdout);
    void* data;
    size_t length;
    
    while (!queue_is_empty(q)) {
        if (queue_dequeue(q, &data, &length)) {
            if (data != NULL && length == sizeof(int)) {
                int value = *(int*)data;
                tprintf("   Dequeued: %d\n", value);
                free(data);
            }
        }
        fflush(stdout);
    }
    
    tprintf("   Queue size after dequeue: %zu\n\n", queue_size(q));
    fflush(stdout);
    
    // Print stats
    tprintf("4. Queue statistics:\n");
    queue_print_stats(q);
    fflush(stdout);
    
    // Cleanup
    tprintf("\n5. Cleaning up...\n");
    fflush(stdout);
    queue_destroy(q);
    tprintf("   Queue destroyed successfully\n");
    fflush(stdout);
    
    tprintf("\nTest completed successfully!\n");
    fflush(stdout);
    
    return 0;
}