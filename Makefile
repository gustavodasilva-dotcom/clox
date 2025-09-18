# Compiler and flags
CC      := gcc
CFLAGS  := -Wall -Wextra -Iinclude
LDFLAGS := 

# Project structure
SRC_DIR := src
OBJ_DIR := build
BIN     := $(OBJ_DIR)/lox

# Source and object files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Default target
all: $(BIN)

# Link the final binary
$(BIN): $(OBJS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Compile each source file into object file
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure build directory exists
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# Run the program
run: all
	./$(BIN)

# Clean up build artifacts
clean:
	rm -rf $(OBJ_DIR)

.PHONY: all clean run
