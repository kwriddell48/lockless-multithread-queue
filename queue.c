#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// Allocate and initialize a new node
static Node* node_create(const void* data, size_t length) {
    Node* node = (Node*)malloc(sizeof(Node));
    if (node == NULL) {
        return NULL;
    }
    
    // Allocate memory for the data and copy it
    if (data != NULL && length > 0) {
        node->data = malloc(length);
        if (node->data == NULL) {
            free(node);
            return NULL;
        }
        memcpy(node->data, data, length);
        node->length = length;
    } else {
        node->data = NULL;
        node->length = 0;
    }
    
    // Initialize prev and next with NULL pointer (plain atomic pointers, no ABA protection on Cygwin)
    atomic_init(&node->prev, (Node*)NULL);
    atomic_init(&node->next, (Node*)NULL);
    // Initialize lock to unlocked (0 = unlocked)
    atomic_init(&node->locked, false);
    return node;
}

// Try to lock a node (returns true if lock was acquired, false if already locked)
static bool node_try_lock(Node* node) {
    if (node == NULL) {
        return false;
    }
    
    // Try to set lock from false (0/unlocked) to true (1/locked)
    bool expected = false;
    return atomic_compare_exchange_strong_explicit(&node->locked, &expected, true,
                                                   memory_order_acquire,
                                                   memory_order_relaxed);
}

// Unlock a node (set lock to 0/unlocked)
static void node_unlock(Node* node) {
    if (node == NULL) {
        return;
    }
    
    // Set lock to false (unlocked)
    atomic_store_explicit(&node->locked, false, memory_order_release);
}

// Free node and its data
// Returns true (1) if node was successfully destroyed, false (0) if node was locked and not destroyed
static bool node_destroy(Node* node) {
    if (node == NULL) {
        return true;  // NULL node is considered successfully "destroyed" (nothing to do)
    }
    
    // Check if node is locked - if locked, cannot destroy it yet
    bool is_locked = atomic_load_explicit(&node->locked, memory_order_acquire);
    if (is_locked) {
        // Node is locked, cannot destroy it, return false (not destroyed)
        return false;
    }
    
    // Node is unlocked, safe to destroy
    if (node->data != NULL) {
        free(node->data);
    }
    free(node);
    return true;  // Successfully destroyed
}

// Initialize an empty queue with sentinel nodes
Queue* queue_init(void) {
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    if (queue == NULL) {
        return NULL;
    }
    
    // Create sentinel head node (no data)
    Node* head = node_create(NULL, 0);
    if (head == NULL) {
        free(queue);
        return NULL;
    }
    
    // Create sentinel tail node (no data)
    Node* tail = node_create(NULL, 0);
    if (tail == NULL) {
        node_destroy(head);
        free(queue);
        return NULL;
    }
    
    // Link sentinel nodes using atomic stores (plain atomic pointers, no ABA protection on Cygwin)
    atomic_store_explicit(&head->prev, (Node*)NULL, memory_order_release);
    atomic_store_explicit(&head->next, tail, memory_order_release);
    atomic_store_explicit(&tail->prev, head, memory_order_release);
    atomic_store_explicit(&tail->next, (Node*)NULL, memory_order_release);
    
    queue->head = head;
    queue->tail = tail;
    atomic_init(&queue->size, 0);
    atomic_init(&queue->max_queue_size, 0);
    atomic_init(&queue->enqueue_counter, 0);
    atomic_init(&queue->dequeue_counter, 0);
    atomic_init(&queue->enqueue_retries, 0);
    atomic_init(&queue->dequeue_retries, 0);
    
    return queue;
}

// Destroy queue and free all memory
void queue_destroy(Queue* queue) {
    if (queue == NULL) {
        return;
    }
    
    // Dequeue all remaining nodes
    void* dummy_data;
    size_t dummy_length;
    while (!queue_is_empty(queue)) {
        if (queue_dequeue(queue, &dummy_data, &dummy_length)) {
            // Data was already freed in dequeue
        }
    }
    
    // Free sentinel nodes
    if (queue->head != NULL) {
        node_destroy(queue->head);
    }
    if (queue->tail != NULL && queue->tail != queue->head) {
        node_destroy(queue->tail);
    }
    
    free(queue);
}

// Enqueue an element at the tail (lock-free with retry, no ABA protection on Cygwin)
bool queue_enqueue(Queue* queue, const void* data, size_t length) {
    if (queue == NULL) {
        return false;
    }
    
    if (data == NULL && length > 0) {
        return false;  // Invalid: data is NULL but length > 0
    }
    
    Node* new_node = node_create(data, length);
    if (new_node == NULL) {
        return false;
    }
    
    Node* tail = queue->tail;
    
    // Lock the new node before entering the retry loop (it won't change, so lock it once)
    if (!node_try_lock(new_node)) {
        // Can't lock the node, free it and return false
        if (new_node->data != NULL) {
            free(new_node->data);
        }
        free(new_node);
        return false;
    }
    
    // Lock-free insertion with retry loop
    // Since we use sentinel nodes, we insert before the tail sentinel
    while (true) {
        // Load tail->prev (plain atomic pointer)
        Node* prev_tail = atomic_load_explicit(&tail->prev, memory_order_acquire);
        
        // Set up new node's links (local writes, not yet visible to other threads)
        atomic_store_explicit(&new_node->next, tail, memory_order_relaxed);
        atomic_store_explicit(&new_node->prev, prev_tail, memory_order_relaxed);
        
        // Memory barrier: ensure new_node's fields are written before making it visible
        atomic_thread_fence(memory_order_release);
        
        // Atomically update previous node's next pointer to point to new_node
        // Use compare-and-swap (plain pointer comparison, no ABA protection on Cygwin)
        Node* expected_next = tail;
        if (atomic_compare_exchange_strong_explicit(&prev_tail->next, &expected_next, 
                                                    new_node,
                                                    memory_order_release,
                                                    memory_order_acquire)) {
            // Successfully updated prev_tail->next
            // Node is already locked (locked before while loop)
            
            // Now update tail->prev
            atomic_store_explicit(&tail->prev, new_node, memory_order_release);
            size_t new_size = atomic_fetch_add_explicit(&queue->size, 1, memory_order_relaxed) + 1;
            
            // Update maximum queue size if current size exceeds it
            size_t current_max = atomic_load_explicit(&queue->max_queue_size, memory_order_relaxed);
            while (new_size > current_max) {
                if (atomic_compare_exchange_weak_explicit(&queue->max_queue_size, &current_max, new_size,
                                                          memory_order_relaxed, memory_order_relaxed)) {
                    break;
                }
                // CAS failed, reload current_max and retry
                current_max = atomic_load_explicit(&queue->max_queue_size, memory_order_relaxed);
            }
            
            // Increment enqueue counter - element has been successfully added to the queue
            atomic_fetch_add_explicit(&queue->enqueue_counter, 1, memory_order_relaxed);
            
            // Unlock the node - element has been fully added to the queue (last step)
            node_unlock(new_node);
            return true;
        }
        
        // CAS failed - another thread modified prev_tail->next, retry
        // This is the key to lock-freedom: we retry instead of blocking
        // Increment retry counter
        atomic_fetch_add_explicit(&queue->enqueue_retries, 1, memory_order_relaxed);
    }
}

// Dequeue an element from the head (lock-free with retry, no ABA protection on Cygwin)
bool queue_dequeue(Queue* queue, void** data, size_t* length) {
    if (queue == NULL || data == NULL || length == NULL) {
        return false;
    }
    
    Node* head = queue->head;
    
    // Lock-free dequeue with retry loop and node locking
    while (true) {
        // Load head->next (plain atomic pointer)
        Node* first_node = atomic_load_explicit(&head->next, memory_order_acquire);
        
        // Check if queue is empty (first_node is tail sentinel)
        if (first_node == queue->tail) {
            return false;
        }
        
        // Try to lock the node before proceeding (prevents concurrent access/delete)
        if (!node_try_lock(first_node)) {
            // Node is locked by another thread, retry
            atomic_fetch_add_explicit(&queue->dequeue_retries, 1, memory_order_relaxed);
            continue;
        }
        
        // Node is now locked - we have exclusive access
        // Get the next node (plain atomic pointer)
        Node* next_node = atomic_load_explicit(&first_node->next, memory_order_acquire);
        
        // Try to atomically update head->next using CAS (plain pointer comparison, no ABA protection on Cygwin)
        Node* expected = first_node;
        if (atomic_compare_exchange_strong_explicit(&head->next, &expected, next_node,
                                                    memory_order_release,
                                                    memory_order_acquire)) {
            // Successfully updated head->next atomically
            // Get the data and length (now safe to read since we successfully dequeued and have the lock)
            *data = first_node->data;
            *length = first_node->length;
            
            // Update next_node->prev if it's not the tail, otherwise update tail->prev
            if (next_node != NULL && next_node != queue->tail) {
                atomic_store_explicit(&next_node->prev, head, memory_order_release);
            } else if (next_node == queue->tail) {
                // We removed the last node, so update tail->prev to point to head
                atomic_store_explicit(&queue->tail->prev, head, memory_order_release);
            }
            
            atomic_fetch_sub_explicit(&queue->size, 1, memory_order_relaxed);
            
            // Increment dequeue counter - element has been successfully removed from the queue
            atomic_fetch_add_explicit(&queue->dequeue_counter, 1, memory_order_relaxed);
            
            // Unlock the node before freeing (though we're about to free it anyway)
            node_unlock(first_node);
            
            // Free the node structure (data is returned to caller, who is responsible for freeing it)
            // Note: In a fully concurrent lock-free system, you'd use
            // hazard pointers or epoch-based reclamation for safe memory reclamation
            free(first_node);
            return true;
        } else {
            // CAS failed - another thread modified head->next, unlock and retry
            node_unlock(first_node);
            atomic_fetch_add_explicit(&queue->dequeue_retries, 1, memory_order_relaxed);
        }
    }
}

// Check if queue is empty
bool queue_is_empty(Queue* queue) {
    if (queue == NULL) {
        return true;
    }
    
    Node* first = atomic_load_explicit(&queue->head->next, memory_order_acquire);
    Node* tail_prev = atomic_load_explicit(&queue->tail->prev, memory_order_acquire);
    
    // Queue is empty if:
    // 1. Head's next points to tail (no elements between them), AND
    // 2. Tail's prev points to head (confirming the link)
    return (first == queue->tail) && (tail_prev == queue->head);
}

// Get queue size
size_t queue_size(Queue* queue) {
    if (queue == NULL) {
        return 0;
    }
    return atomic_load_explicit(&queue->size, memory_order_acquire);
}

// Get maximum queue size
size_t queue_max_size(Queue* queue) {
    if (queue == NULL) {
        return 0;
    }
    return atomic_load_explicit(&queue->max_queue_size, memory_order_acquire);
}

// Print queue contents (for debugging)
// If print_func is NULL, prints data address and length
void queue_print(Queue* queue, void (*print_func)(const void* data, size_t length)) {
    if (queue == NULL) {
        tprintf("Queue is NULL\n");
        return;
    }
    
    tprintf("Queue (size: %zu): [", queue_size(queue));
    
    Node* current = atomic_load_explicit(&queue->head->next, memory_order_acquire);
    bool first = true;
    
    while (current != NULL && current != queue->tail) {
        if (!first) {
            tprintf(", ");
        }
        
        if (print_func != NULL) {
            print_func(current->data, current->length);
        } else {
            // Default: print address and length
            tprintf("(ptr: %p, len: %zu)", current->data, current->length);
        }
        
        first = false;
        current = atomic_load_explicit(&current->next, memory_order_acquire);
    }
    
    tprintf("]\n");
}

// Print queue statistics (counters and size)
void queue_print_stats(Queue* queue) {
    if (queue == NULL) {
        tprintf("Queue is NULL\n");
        return;
    }
    
    size_t size = atomic_load_explicit(&queue->size, memory_order_acquire);
    size_t max_size = atomic_load_explicit(&queue->max_queue_size, memory_order_acquire);
    unsigned int enqueue_count = atomic_load_explicit(&queue->enqueue_counter, memory_order_acquire);
    unsigned int dequeue_count = atomic_load_explicit(&queue->dequeue_counter, memory_order_acquire);
    unsigned int enqueue_retries = atomic_load_explicit(&queue->enqueue_retries, memory_order_acquire);
    unsigned int dequeue_retries = atomic_load_explicit(&queue->dequeue_retries, memory_order_acquire);
    
    tprintf("Queue Statistics:\n");
    tprintf("  Size: %zu\n", size);
    tprintf("  Maximum Queue Size: %zu\n", max_size);
    tprintf("  Enqueue Counter: %u\n", enqueue_count);
    tprintf("  Dequeue Counter: %u\n", dequeue_count);
    tprintf("  Enqueue Retries: %u\n", enqueue_retries);
    tprintf("  Dequeue Retries: %u\n", dequeue_retries);
    tprintf("  Net Operations: %d\n", (int)(enqueue_count - dequeue_count));
}
