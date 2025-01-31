# Variables
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
TARGET = aesdsocket

# Targets
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)
	rm -f $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

# Optional: Phony targets
.PHONY: all clean
