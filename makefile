# Makefile for MyShell Phase 2 - Universal Binary (Intel + M1)
CC = gcc
CFLAGS = -Wall -g -Iinclude -arch x86_64 -arch arm64

# Source files
MAIN_SRC    = src/main.c
PARSER_SRC  = src/parser.c
EXECUTOR_SRC= src/executor.c
SERVER_SRC  = src/server.c
CLIENT_SRC  = src/client.c

# Object files
MAIN_OBJ    = src/main.o
PARSER_OBJ  = src/parser.o
EXECUTOR_OBJ= src/executor.o
SERVER_OBJ  = src/server.o
CLIENT_OBJ  = src/client.o

# Targets
MYSHELL_TARGET = myshell
SERVER_TARGET  = server
CLIENT_TARGET  = client

# Default target: build everything
all: $(MYSHELL_TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)

# Build myshell executable
$(MYSHELL_TARGET): $(MAIN_OBJ) $(PARSER_OBJ) $(EXECUTOR_OBJ)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "myshell compiled successfully! Run with: ./myshell"

# Build server executable
$(SERVER_TARGET): $(SERVER_OBJ) $(PARSER_OBJ) $(EXECUTOR_OBJ)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Server compiled successfully! Run with: ./server"

# Build client executable
$(CLIENT_TARGET): $(CLIENT_OBJ) $(PARSER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Client compiled successfully! Run with: ./client"

# Compile source files
$(MAIN_OBJ): $(MAIN_SRC) include/parser.h include/executor.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_OBJ): $(SERVER_SRC) include/server.h include/parser.h include/executor.h
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT_OBJ): $(CLIENT_SRC) include/client.h include/parser.h
	$(CC) $(CFLAGS) -c $< -o $@

$(PARSER_OBJ): $(PARSER_SRC) include/parser.h
	$(CC) $(CFLAGS) -c $< -o $@

$(EXECUTOR_OBJ): $(EXECUTOR_SRC) include/executor.h include/parser.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(MAIN_OBJ) $(SERVER_OBJ) $(CLIENT_OBJ) $(PARSER_OBJ) $(EXECUTOR_OBJ) $(MYSHELL_TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)
	@echo "Cleaned build files"

# Rebuild everything from scratch
rebuild: clean all

# Help target
help:
	@echo "MyShell Phase 2 - Universal Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  make          - Build myshell, server, and client (default)"
	@echo "  make myshell  - Build myshell only"
	@echo "  make server   - Build server only"
	@echo "  make client   - Build client only"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make rebuild  - Clean and rebuild all"
	@echo "  make help     - Show this help message"

.PHONY: all clean rebuild help myshell server client
