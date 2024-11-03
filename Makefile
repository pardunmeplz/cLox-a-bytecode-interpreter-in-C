# GPT generated because my hind can not understand this makefile business

CC = gcc

CFLAGS = -Wall -g -Iinclude

SRC_DIR = src
INC_DIR = include

SRC =  $(wildcard $(SRC_DIR)/*.c)

EXEC = c_lox

all: $(EXEC)

$(EXEC) : $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(EXEC)

clean:
	rm -f $(EXEC)

.PHONY: all clean
