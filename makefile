# # Compiler and flags
# CC = gcc
# CFLAGS = -Wall -g -Iinclude -pthread
# LDFLAGS = -pthread

# # Source files
# SRC_MAIN = src/main.c src/parser.c src/executor.c
# SRC_SERVER = src/server.c src/parser.c src/executor.c
# SRC_CLIENT = src/client.c

# # Object files
# OBJ_MAIN = $(SRC_MAIN:.c=.o)
# OBJ_SERVER = $(SRC_SERVER:.c=.o)
# OBJ_CLIENT = $(SRC_CLIENT:.c=.o)

# # Targets
# TARGET_SHELL = myshell
# TARGET_SERVER = server
# TARGET_CLIENT = client

# # Default target: build all executables
# all: $(TARGET_SHELL) $(TARGET_SERVER) $(TARGET_CLIENT)

# # Build Phase 1 shell
# $(TARGET_SHELL): $(OBJ_MAIN)
# 	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# # Build Phase 3 server (multithreaded)
# $(TARGET_SERVER): $(OBJ_SERVER)
# 	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# # Build client
# $(TARGET_CLIENT): $(OBJ_CLIENT)
# 	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# # Compile each .c file into a .o file
# %.o: %.c
# 	$(CC) $(CFLAGS) -c $< -o $@

# # Clean build artifacts
# clean:
# 	rm -f $(OBJ_MAIN) $(OBJ_SERVER) $(OBJ_CLIENT) \
# 	       $(TARGET_SHELL) $(TARGET_SERVER) $(TARGET_CLIENT)

# # Declare phony targets
# .PHONY: all clean



# Makefile - Phase 4: Server with Scheduling Capabilities
# Compiles all components including the scheduler and demo program

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread
LDFLAGS = -pthread

# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

# Source files
SERVER_SRCS = $(SRC_DIR)/server.c $(SRC_DIR)/scheduler.c $(SRC_DIR)/parser.c $(SRC_DIR)/executor.c
CLIENT_SRCS = $(SRC_DIR)/client.c
DEMO_SRC = demo.c

# Object files
SERVER_OBJS = $(OBJ_DIR)/server.o $(OBJ_DIR)/scheduler.o $(OBJ_DIR)/parser.o $(OBJ_DIR)/executor.o
CLIENT_OBJS = $(OBJ_DIR)/client.o

# Executables
SERVER = server
CLIENT = client
DEMO = demo

# Default target: build all
all: $(OBJ_DIR) $(SERVER) $(CLIENT) $(DEMO)

# Create object directory if it doesn't exist
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Server executable
$(SERVER): $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Client executable
$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Demo program
$(DEMO): $(DEMO_SRC)
	$(CC) $(CFLAGS) -o $@ $<

# Object file compilation rules
$(OBJ_DIR)/server.o: $(SRC_DIR)/server.c $(INC_DIR)/server.h $(INC_DIR)/scheduler.h
	$(CC) $(CFLAGS) -I$(INC_DIR) -c -o $@ $<

$(OBJ_DIR)/scheduler.o: $(SRC_DIR)/scheduler.c $(INC_DIR)/scheduler.h $(INC_DIR)/server.h
	$(CC) $(CFLAGS) -I$(INC_DIR) -c -o $@ $<

$(OBJ_DIR)/parser.o: $(SRC_DIR)/parser.c $(INC_DIR)/parser.h
	$(CC) $(CFLAGS) -I$(INC_DIR) -c -o $@ $<

$(OBJ_DIR)/executor.o: $(SRC_DIR)/executor.c $(INC_DIR)/executor.h $(INC_DIR)/parser.h
	$(CC) $(CFLAGS) -I$(INC_DIR) -c -o $@ $<

$(OBJ_DIR)/client.o: $(SRC_DIR)/client.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c -o $@ $<

# Clean build artifacts
clean:
	rm -rf $(OBJ_DIR) $(SERVER) $(CLIENT) $(DEMO)

# Rebuild from scratch
rebuild: clean all

# Run server
run-server: $(SERVER)
	./$(SERVER)

# Run client
run-client: $(CLIENT)
	./$(CLIENT)

# Help target
help:
	@echo "Available targets:"
	@echo "  all        - Build server, client, and demo (default)"
	@echo "  server     - Build only the server"
	@echo "  client     - Build only the client"
	@echo "  demo       - Build only the demo program"
	@echo "  clean      - Remove all build artifacts"
	@echo "  rebuild    - Clean and rebuild everything"
	@echo "  run-server - Build and run the server"
	@echo "  run-client - Build and run the client"
	@echo "  help       - Show this help message"

.PHONY: all clean rebuild run-server run-client help