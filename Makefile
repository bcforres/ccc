# CCC Makefile
# Some parts from here: https://github.com/mbcrawfo/GenericMakefile

BIN_NAME := ccc
CC ?= cc
DEBUG_FLAGS = -DDEBUG
CFLAGS = -std=c11 -Wall -Wextra -Werror -D_DEFAULT_SOURCE=1 -g -O0
LDFLAGS = -lm

SRC = src
DEST = bin
BUILD_DIR = $(DEST)/build

INC = -I$(SRC)/


# Find all source files in the source directory
SOURCES := $(shell find $(SRC) -name '*.c')

OBJS := $(SOURCES:$(SRC)/%.c=$(BUILD_DIR)/%.o)
# Set the dependency files that will be used to add header dependencies
DEPS := $(OBJS:.o=.d)

all: dirs
	@$(MAKE) $(DEST)/$(BIN_NAME) --no-print-directory

# Create the directories used in the build
.PHONY: dirs
dirs:
	@mkdir -p $(dir $(OBJS))

$(DEST)/$(BIN_NAME): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

.PHONY: clean
clean:
	$(RM) -r $(DEST)/*

.PHONY: test
test: all
	@./test/scripts/test_all.bash

# Add dependency files, if they exist
-include $(DEPS)

$(BUILD_DIR)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) $(INC) -MP -MMD -c $< -o $@
