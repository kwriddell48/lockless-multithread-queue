CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -pthread
LDFLAGS = -latomic
TARGET = queue_demo
THREAD_TEST_TARGET = thread_test
SOURCES = main.c queue.c
THREAD_TEST_SOURCES = thread_test.c queue.c
OBJECTS = $(SOURCES:.c=.o)
THREAD_TEST_OBJECTS = $(THREAD_TEST_SOURCES:.c=.o)

.PHONY: all clean run run-thread-test

all: $(TARGET) $(THREAD_TEST_TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

$(THREAD_TEST_TARGET): $(THREAD_TEST_OBJECTS)
	$(CC) $(CFLAGS) -o $(THREAD_TEST_TARGET) $(THREAD_TEST_OBJECTS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(THREAD_TEST_OBJECTS) $(TARGET) $(THREAD_TEST_TARGET)

run: $(TARGET)
	./$(TARGET)

run-thread-test: $(THREAD_TEST_TARGET)
	./$(THREAD_TEST_TARGET)
