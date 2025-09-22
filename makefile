CC = gcc
CFLAGS = -Wall -g -Iinclude

SRC = src/main.c src/parser.c src/executor.c
OBJ = $(SRC:.c=.o)
TARGET = myshell

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJ) $(TARGET)
