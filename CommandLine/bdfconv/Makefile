TARGET := bdfconv

SRC_DIR := src
INC_DIR := inc
BUILD_DIR := build
BIN_DIR := bin
HEADERS_DIR := ../../Headers

INC_FLAGS := -I $(INC_DIR) -I $(HEADERS_DIR) -I /usr/include/freetype2

CC := gcc
LD := gcc

OPT_LEVEL ?= 3
EXTRA ?= -march=x86-64 -mtune=generic -funroll-loops -w

CFLAGS := -O$(OPT_LEVEL) $(EXTRA) -Wall -Wextra $(INC_FLAGS)
LDFLAGS :=
LDLIBS := -lfreetype

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

OUTPUT := $(BIN_DIR)/$(TARGET)

RESET := \033[0m
BOLD := \033[1m
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
BLUE := \033[34m
MAGENTA := \033[35m
CYAN := \033[36m

.PHONY: all clean

all: $(BIN_DIR) $(BUILD_DIR) $(OUTPUT)
	@printf "$(BOLD)$(GREEN)Build complete:$(RESET) $(OUTPUT)\n"

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(OUTPUT): $(OBJECTS)
	@printf "$(BLUE)$(BOLD)Linking$(RESET) %s\n" "$@"
	@$(LD) $(LDFLAGS) -o $(OUTPUT) $(OBJECTS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@printf "$(CYAN)$(BOLD)Compiling$(RESET) %s -> %s\n" "$<" "$@"
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@printf "$(YELLOW)$(BOLD)CLEAN$(RESET)  Removing build artifacts...\n"
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@printf "$(GREEN)Done.$(RESET)\n"
