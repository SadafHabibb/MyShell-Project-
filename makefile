# CC = gcc
# CFLAGS = -Wall -g -Iinclude

# SRC = src/main.c src/parser.c src/executor.c
# OBJ = $(SRC:.c=.o)
# TARGET = myshell

# all: $(TARGET)

# $(TARGET): $(OBJ)
# 	$(CC) $(CFLAGS) -o $@ $^

# clean:
# 	rm -f $(OBJ) $(TARGET)

# Makefile for MyShell Phase 2
# Compiles server program that uses Phase 1 parser and executor

CC = gcc
CFLAGS = -Wall -g -Iinclude

# Source files
PARSER_SRC = src/parser.c
EXECUTOR_SRC = src/executor.c
SERVER_SRC = src/server.c

# Object files
PARSER_OBJ = src/parser.o
EXECUTOR_OBJ = src/executor.o
SERVER_OBJ = src/server.o

# Targets
SERVER_TARGET = server

# Default target: build server
all: $(SERVER_TARGET)

# Build server executable
$(SERVER_TARGET): $(SERVER_OBJ) $(PARSER_OBJ) $(EXECUTOR_OBJ)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Server compiled successfully!"
	@echo "Run with: ./server"

# Compile server.c
$(SERVER_OBJ): $(SERVER_SRC) include/server.h include/parser.h include/executor.h
	$(CC) $(CFLAGS) -c $(SERVER_SRC) -o $@

# Compile parser.c
$(PARSER_OBJ): $(PARSER_SRC) include/parser.h
	$(CC) $(CFLAGS) -c $(PARSER_SRC) -o $@

# Compile executor.c
$(EXECUTOR_OBJ): $(EXECUTOR_SRC) include/executor.h include/parser.h
	$(CC) $(CFLAGS) -c $(EXECUTOR_SRC) -o $@

# Clean build artifacts
clean:
	rm -f $(SERVER_OBJ) $(PARSER_OBJ) $(EXECUTOR_OBJ) $(SERVER_TARGET)
	@echo "Cleaned build files"

# Rebuild everything from scratch
rebuild: clean all

# Help target
help:
	@echo "MyShell Phase 2 - Server Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  make          - Build server (default)"
	@echo "  make all      - Build server"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make rebuild  - Clean and rebuild"
	@echo "  make help     - Show this help message"

.PHONY: all clean rebuild help