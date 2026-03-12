CC ?= gcc
PKG_CONFIG ?= pkg-config
TARGET := ../server
SRC := $(wildcard ../source/*.c)
INC_DIR := ../inc
CFLAGS ?= -Wall -g
PKG_CONFIG_EXISTS := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo yes || echo no)

ifeq ($(PKG_CONFIG_EXISTS),yes)
EVENT_CFLAGS := $(shell $(PKG_CONFIG) --cflags libevent 2>/dev/null)
EVENT_LIBS := $(shell $(PKG_CONFIG) --libs libevent 2>/dev/null)
endif

.DEFAULT_GOAL := all

all: check-env $(TARGET)

$(TARGET): $(SRC)
	@printf '%s\n' "Building $(TARGET) ..."
	$(CC) $(CFLAGS) -I$(INC_DIR) $(EVENT_CFLAGS) $^ -o $@ $(EVENT_LIBS)

check-env:
ifeq ($(PKG_CONFIG_EXISTS),no)
	@printf '%s\n' "Error: pkg-config is not installed on this server."
	@printf '%s\n' "Hint: sudo apt update && sudo apt install -y build-essential pkg-config libevent-dev"
	@false
endif
ifeq ($(strip $(EVENT_LIBS)),)
	@printf '%s\n' "Error: libevent development files were not found by pkg-config."
	@printf '%s\n' "Hint: sudo apt update && sudo apt install -y build-essential pkg-config libevent-dev"
	@printf '%s\n' "Check: pkg-config --modversion libevent"
	@false
endif

help:
	@printf '%s\n' "Targets:"
	@printf '%s\n' "  make         Build ../server after checking pkg-config and libevent"
	@printf '%s\n' "  make clean   Remove ../server"
	@printf '%s\n' "  make help    Show this help"

clean:
	$(RM) $(TARGET)

.PHONY: all check-env help clean
