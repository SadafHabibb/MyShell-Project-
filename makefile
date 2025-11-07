# Compiler and flags
CC = gcc
CFLAGS = -Wall -g -Iinclude -pthread
LDFLAGS = -pthread

# Source files
SRC_MAIN = src/main.c src/parser.c src/executor.c
SRC_SERVER = src/server.c src/parser.c src/executor.c
SRC_CLIENT = src/client.c

# Object files
OBJ_MAIN = $(SRC_MAIN:.c=.o)
OBJ_SERVER = $(SRC_SERVER:.c=.o)
OBJ_CLIENT = $(SRC_CLIENT:.c=.o)

# Targets
TARGET_SHELL = myshell
TARGET_SERVER = server
TARGET_CLIENT = client

# Default target: build all executables
all: $(TARGET_SHELL) $(TARGET_SERVER) $(TARGET_CLIENT)

# Build Phase 1 shell
$(TARGET_SHELL): $(OBJ_MAIN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build Phase 3 server (multithreaded)
$(TARGET_SERVER): $(OBJ_SERVER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build client
$(TARGET_CLIENT): $(OBJ_CLIENT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile each .c file into a .o file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJ_MAIN) $(OBJ_SERVER) $(OBJ_CLIENT) \
	       $(TARGET_SHELL) $(TARGET_SERVER) $(TARGET_CLIENT)

# Declare phony targets
.PHONY: all clean

