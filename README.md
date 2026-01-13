# Lock-Free Doubly Linked List Queue

A C implementation of a lock-free doubly linked list queue using atomic operations and memory ordering semantics from C11.

## Features

- **Lock-Free**: Uses atomic operations and compare-and-swap (CAS) instead of mutexes or locks
- **Doubly Linked List**: Maintains both forward and backward links for each node
- **ABA Protection**: Each pointer field (`prev`, `next`) includes an ABA (Address-Based Addressing) counter that is incremented on each pointer change, preventing the ABA problem in lock-free algorithms
- **Atomic Operations**: Uses C11 `stdatomic.h` for thread-safe operations
- **Memory Ordering**: Properly uses memory barriers and ordering semantics
- **Retry Logic**: Implements retry loops for true lock-freedom

## Building

```bash
make
```

## Running

```bash
make run
# or
./queue_demo
```

## Usage

```c
#include "queue.h"

// Initialize queue
Queue* queue = queue_init();

// Enqueue elements (data is copied into the queue)
int value1 = 10;
queue_enqueue(queue, &value1, sizeof(int));

const char* str = "Hello";
queue_enqueue(queue, str, strlen(str) + 1);  // Include null terminator

// Dequeue elements
void* data;
size_t length;
if (queue_dequeue(queue, &data, &length)) {
    // Use the data (caller is responsible for freeing it)
    int* value = (int*)data;
    printf("Dequeued: %d\n", *value);
    free(data);  // Free the data returned by dequeue
}

// Check status
if (queue_is_empty(queue)) {
    // handle empty queue
}

size_t size = queue_size(queue);

// Print with custom function
void print_int(const void* data, size_t length) {
    if (length == sizeof(int)) {
        printf("%d", *(const int*)data);
    }
}
queue_print(queue, print_int);

// Clean up
queue_destroy(queue);
```

## API

- `Queue* queue_init(void)` - Initialize an empty queue
- `void queue_destroy(Queue* queue)` - Destroy queue and free all memory
- `bool queue_enqueue(Queue* queue, const void* data, size_t length)` - Add element to tail of queue (data is copied)
- `bool queue_dequeue(Queue* queue, void** data, size_t* length)` - Remove element from head of queue (caller must free the returned data)
- `bool queue_is_empty(Queue* queue)` - Check if queue is empty
- `size_t queue_size(Queue* queue)` - Get current queue size
- `void queue_print(Queue* queue, void (*print_func)(const void* data, size_t length))` - Print queue contents (pass NULL for default format)
- `void queue_print_stats(Queue* queue)` - Print queue statistics (size, counters, retry counts)

## Implementation Details

- Uses **sentinel nodes** (dummy head and tail nodes) to simplify edge cases
- Implements **lock-free** enqueue and dequeue using atomic compare-and-swap operations
- **ABA Protection Mechanism**: Each `prev` and `next` pointer field is paired with an `unsigned int` ABA counter:
  - The `PointerWithABA` structure combines the pointer and ABA counter
  - The ABA counter is incremented every time a pointer is changed
  - CAS operations compare both the pointer AND the ABA counter together
  - This prevents the ABA problem where a pointer value appears unchanged but the underlying object has been freed and reallocated
- Uses **memory ordering semantics** (acquire/release) to ensure proper synchronization
- Retry loops ensure progress even when CAS operations fail due to concurrent modifications
- **Generic data storage**: Each node stores a `void*` pointer to data and its `size_t` length
- **Data copying**: Data is copied into the queue on enqueue, so the original data can be modified or freed
- **Memory management**: Caller is responsible for freeing data returned by `dequeue()`
- **Performance tracking**: Includes counters for successful operations and retry attempts, useful for analyzing lock-free performance under contention

## Notes

- This implementation is designed to be lock-free and can be used in concurrent scenarios
- **Platform Compatibility**: On platforms without 16-byte atomic support (like Cygwin), the implementation uses plain atomic pointers without ABA protection for compatibility
- Memory reclamation uses standard `free()` - for production concurrent use, consider hazard pointers or epoch-based reclamation
- Requires C11 compiler support for `stdatomic.h`
- The queue copies data on enqueue, so the caller can free their original data after enqueuing
- The caller must free data returned by `dequeue()` to prevent memory leaks
- Supports any data type (integers, strings, structs, etc.) by passing pointer and size
- **Statistics**: The queue tracks enqueue/dequeue counters and retry counts for performance analysis## Building on Different Platforms### Linux/Unix
```bash
make
```

### Windows (Cygwin/MinGW)
```bash
make
```
Note: Requires linking with `-latomic` for 16-byte atomic operations on some platforms.

### Windows (MSVC)
```batch
cl /W4 /std:c11 /O2 main.c queue.c /Fe:queue_demo.exe
```

## Testing

The included test program demonstrates:
- Basic queue operations (enqueue/dequeue)
- String and integer data types
- Multi-threaded concurrent access (10 threads, 100 items each)
- Performance statistics including retry counts
