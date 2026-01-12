#ifndef QUEUE_H
#define QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

// Structure to hold pointer with ABA prevention version counter
typedef struct PointerWithABA {
    struct Node* ptr;     // The actual pointer
    unsigned int aba;     // ABA version counter (incremented on each pointer change)
} PointerWithABA;

// Compile-time check to ensure PointerWithABA is lock-free
// This structure should fit in 16 bytes on 64-bit systems (8-byte pointer + 4-byte int + padding)
// If atomic operations on this struct are not lock-free, the implementation may need adjustment
static_assert(sizeof(PointerWithABA) <= 16, "PointerWithABA structure is too large for efficient atomic operations");

// Node structure for doubly linked list
// Note: On platforms without 16-byte atomic support (like Cygwin), we fall back
// to using plain atomic pointers without ABA protection
typedef struct Node {
    void* data;           // Pointer to the data object
    size_t length;        // Length of the data object in bytes
    _Atomic(struct Node*) prev;  // Previous node pointer (ABA counter removed for Cygwin compatibility)
    _Atomic(struct Node*) next;  // Next node pointer (ABA counter removed for Cygwin compatibility)
} Node;

// Queue structure
typedef struct Queue {
    Node* head;  // Sentinel head node (stable, doesn't change)
    Node* tail;  // Sentinel tail node (stable, doesn't change)
    atomic_size_t size;
    atomic_uint enqueue_counter;  // Counter for successful enqueue operations (unsigned int)
    atomic_uint dequeue_counter;  // Counter for successful dequeue operations (unsigned int)
    atomic_uint enqueue_retries;  // Counter for enqueue retry attempts (CAS failures)
    atomic_uint dequeue_retries;  // Counter for dequeue retry attempts (CAS failures)
} Queue;

// Function declarations
Queue* queue_init(void);
void queue_destroy(Queue* queue);
bool queue_enqueue(Queue* queue, const void* data, size_t length);
bool queue_dequeue(Queue* queue, void** data, size_t* length);
bool queue_is_empty(Queue* queue);
size_t queue_size(Queue* queue);
void queue_print(Queue* queue, void (*print_func)(const void* data, size_t length));
void queue_print_stats(Queue* queue);

#endif // QUEUE_H
