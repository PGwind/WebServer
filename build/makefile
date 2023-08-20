src = $(wildcard ../source/*.c)
myArgs = -Wall -g
target = server
CC = gcc
INC_DIR = ../inc

ALL: $(target)

$(target): $(src)
	$(CC) $^ -o ../$@ $(myArgs) -levent -I$(INC_DIR)

clean:
	-rm -rf $(target)

.PHONY: clean ALL

