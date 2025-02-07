# Variables
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall
CFLAGS += -I/usr/include/x86_64-linux-gnu
LFLAGS = -lbsd -pthread
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
TARGET = aesdsocket

# Targets
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $(OBJ)
	rm -f $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(LFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

# Optional: Phony targets
.PHONY: all clean
