#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    return node;
}

// Free node and its data
static void node_destroy(Node* node) {
    if (node != NULL) {
        if (node->data != NULL) {
            free(node->data);
        }
        free(node);
    }
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
            // Now update tail->prev
            atomic_store_explicit(&tail->prev, new_node, memory_order_release);
            atomic_fetch_add_explicit(&queue->size, 1, memory_order_relaxed);
            
            // Increment enqueue counter - element has been successfully added to the queue
            atomic_fetch_add_explicit(&queue->enqueue_counter, 1, memory_order_relaxed);
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
    
    // Lock-free dequeue with retry loop
    while (true) {
        // Load head->next (plain atomic pointer)
        Node* first_node = atomic_load_explicit(&head->next, memory_order_acquire);
        
        // Check if queue is empty (first_node is tail sentinel)
        if (first_node == queue->tail) {
            return false;
        }
        
        // Get the next node (plain atomic pointer)
        Node* next_node = atomic_load_explicit(&first_node->next, memory_order_acquire);
        
        // Try to atomically update head->next using CAS (plain pointer comparison, no ABA protection on Cygwin)
        Node* expected = first_node;
        if (atomic_compare_exchange_strong_explicit(&head->next, &expected, next_node,
                                                    memory_order_release,
                                                    memory_order_acquire)) {
            // Successfully updated head->next atomically
            // Get the data and length (now safe to read since we successfully dequeued)
            *data = first_node->data;
            *length = first_node->length;
            
            // Update next_node->prev if it's not the tail
            if (next_node != NULL && next_node != queue->tail) {
                atomic_store_explicit(&next_node->prev, head, memory_order_release);
            }
            
            atomic_fetch_sub_explicit(&queue->size, 1, memory_order_relaxed);
            
            // Increment dequeue counter - element has been successfully removed from the queue
            atomic_fetch_add_explicit(&queue->dequeue_counter, 1, memory_order_relaxed);
            
            // Free the node structure (data is returned to caller, who is responsible for freeing it)
            // Note: In a fully concurrent lock-free system, you'd use
            // hazard pointers or epoch-based reclamation for safe memory reclamation
            free(first_node);
            return true;
        }
        
        // CAS failed - another thread modified head->next, retry
        // This is the key to lock-freedom: we retry instead of blocking
        // Increment retry counter
        atomic_fetch_add_explicit(&queue->dequeue_retries, 1, memory_order_relaxed);
    }
}

// Check if queue is empty
bool queue_is_empty(Queue* queue) {
    if (queue == NULL) {
        return true;
    }
    
    size_t size = atomic_load_explicit(&queue->size, memory_order_acquire);
    Node* first = atomic_load_explicit(&queue->head->next, memory_order_acquire);
    return (size == 0) || (first == queue->tail);
}

// Get queue size
size_t queue_size(Queue* queue) {
    if (queue == NULL) {
        return 0;
    }
    return atomic_load_explicit(&queue->size, memory_order_acquire);
}

// Print queue contents (for debugging)
// If print_func is NULL, prints data address and length
void queue_print(Queue* queue, void (*print_func)(const void* data, size_t length)) {
    if (queue == NULL) {
        printf("Queue is NULL\n");
        return;
    }
    
    printf("Queue (size: %zu): [", queue_size(queue));
    
    Node* current = atomic_load_explicit(&queue->head->next, memory_order_acquire);
    bool first = true;
    
    while (current != NULL && current != queue->tail) {
        if (!first) {
            printf(", ");
        }
        
        if (print_func != NULL) {
            print_func(current->data, current->length);
        } else {
            // Default: print address and length
            printf("(ptr: %p, len: %zu)", current->data, current->length);
        }
        
        first = false;
        current = atomic_load_explicit(&current->next, memory_order_acquire);
    }
    
    printf("]\n");
}

// Print queue statistics (counters and size)
void queue_print_stats(Queue* queue) {
    if (queue == NULL) {
        printf("Queue is NULL\n");
        return;
    }
    
    size_t size = atomic_load_explicit(&queue->size, memory_order_acquire);
    unsigned int enqueue_count = atomic_load_explicit(&queue->enqueue_counter, memory_order_acquire);
    unsigned int dequeue_count = atomic_load_explicit(&queue->dequeue_counter, memory_order_acquire);
    unsigned int enqueue_retries = atomic_load_explicit(&queue->enqueue_retries, memory_order_acquire);
    unsigned int dequeue_retries = atomic_load_explicit(&queue->dequeue_retries, memory_order_acquire);
    
    printf("Queue Statistics:\n");
    printf("  Size: %zu\n", size);
    printf("  Enqueue Counter: %u\n", enqueue_count);
    printf("  Dequeue Counter: %u\n", dequeue_count);
    printf("  Enqueue Retries: %u\n", enqueue_retries);
    printf("  Dequeue Retries: %u\n", dequeue_retries);
    printf("  Net Operations: %d\n", (int)(enqueue_count - dequeue_count));
}
