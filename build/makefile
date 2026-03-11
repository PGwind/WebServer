CC ?= gcc
TARGET := ../server
SRC := $(wildcard ../source/*.c)
INC_DIR := ../inc
CFLAGS ?= -Wall -g
EVENT_CFLAGS := $(shell pkg-config --cflags libevent 2>/dev/null)
EVENT_LIBS := $(shell pkg-config --libs libevent 2>/dev/null)

ifeq ($(strip $(EVENT_LIBS)),)
EVENT_LIBS := -levent
endif

.DEFAULT_GOAL := all

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -I$(INC_DIR) $(EVENT_CFLAGS) $^ -o $@ $(EVENT_LIBS)

clean:
	$(RM) $(TARGET)

.PHONY: all clean
