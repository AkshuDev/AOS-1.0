# Colors
CLR_BLACK   = \033[0;30m
CLR_RED     = \033[0;31m
CLR_GREEN   = \033[0;32m
CLR_YELLOW  = \033[0;33m
CLR_BLUE    = \033[0;34m
CLR_MAGENTA = \033[0;35m
CLR_CYAN    = \033[0;36m
CLR_WHITE   = \033[0;37m
CLR_GRAY    = \033[1;30m
CLR_RESET   = \033[0m

CC := gcc

EXTRA ?= -march=x86-64 -mtune=generic -funroll-loops -mcmodel=large
OPT_LEVEL ?= 3
CFLAGS := -ffreestanding -m64 -fno-stack-protector -fno-pic \
		-Wall -nostdlib -fno-pie \
		-I ../PBFS -I ../Headers -I ./ \
		-O$(OPT_LEVEL) $(EXTRA)

ASM := nasm
ASMFLAGS := -f elf64

LD := ld
LDFLAGS := -m elf_x86_64 -nostdlib --nmagic --omagic -L ../PBFS/PBFS/build-fs -T linker.ld
LDPBFS := ../PBFS/PBFS/build-fs/libpbfs-ndrivers.a

OBJCOPY := objcopy

BUILD_DIR := build
BIN_DIR := bin
SRC_DIR := src

KERNEL := $(wildcard $(SRC_DIR)/*.c)
KERNEL_O := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL))
KERNEL_ELF := $(BIN_DIR)/aos.elf

DRIVERS_DIR := drivers
DRIVERS := $(shell find $(DRIVERS_DIR)/ -name '*.c')
DRIVERS_O := $(patsubst $(DRIVERS_DIR)/%.c,$(BUILD_DIR)/%.o,$(DRIVERS))

MEMORY_DIR := mm
MEMORY_FILES := $(wildcard $(MEMORY_DIR)/*.c)
MEMORY_FILES_O := $(patsubst $(MEMORY_DIR)/%.c,$(BUILD_DIR)/%.o,$(MEMORY_FILES))

ARCH_DIR := src/arch
ASM_x86_64_DIR := $(ARCH_DIR)/x86_64
ASM_x86_64_FILES := $(wildcard $(ASM_x86_64_DIR)/*.asm)
ASM_x86_64_FILES_O := $(patsubst $(ASM_x86_64_DIR)/%.asm,$(BUILD_DIR)/%.o,$(ASM_x86_64_FILES))

.PHONY: all clean

all: $(BUILD_DIR) $(BIN_DIR) $(KERNEL_ELF)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@printf "$(CLR_YELLOW) ---- Compiling Core Kernel Source File: \"$<\"$(CLR_RESET)\n"
	@$(CC) $(CFLAGS) -c $< -o $@
	@printf "$(CLR_GREEN) ---- Compiled to \"$@\"$(CLR_RESET)\n"

$(BUILD_DIR)/%.o: $(DRIVERS_DIR)/%.c
	@printf "$(CLR_YELLOW) ---- Compiling Core Kernel Driver File: \"$<\"$(CLR_RESET)\n"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@printf "$(CLR_GREEN) ---- Compiled to \"$@\"$(CLR_RESET)\n"

$(BUILD_DIR)/%.o: $(MEMORY_DIR)/%.c
	@printf "$(CLR_YELLOW) ---- Compiling Core Kernel MM File: \"$<\"$(CLR_RESET)\n"
	@$(CC) $(CFLAGS) -c $< -o $@
	@printf "$(CLR_GREEN) ---- Compiled to \"$@\"$(CLR_RESET)\n"

$(BUILD_DIR)/%.o: $(ASM_x86_64_DIR)/%.asm
	@printf "$(CLR_YELLOW) ---- Assembling Core Kernel Source File: \"$<\"$(CLR_RESET)\n"
	@$(ASM) $(ASMFLAGS) $< -o $@
	@printf "$(CLR_GREEN) ---- Assembled to \"$@\"$(CLR_RESET)\n"

$(KERNEL_ELF): $(KERNEL_O) $(DRIVERS_O) $(MEMORY_FILES_O) $(ASM_x86_64_FILES_O)
	@printf "$(CLR_YELLOW) -- Compiling PBFS$(CLR_RESET)\n"
	@make -C ../PBFS/PBFS/src-fs clean
	@make -C ../PBFS/PBFS/src-fs
	@printf "$(CLR_GREEN) -- Compiled PBFS$(CLR_RESET)\n"

	@printf "$(CLR_YELLOW) -- Linking Core Kernel$(CLR_RESET)\n"
	@$(LD) $(LDFLAGS) $(KERNEL_O) $(MEMORY_FILES_O) $(ASM_x86_64_FILES_O) $(DRIVERS_O) $(LDPBFS) -o $(KERNEL_ELF)
	@printf "$(CLR_GREEN) -- Linked to \"$@\"$(CLR_RESET)\n"

$(LDPBFS): ../PBFS/PBFS/src-fs/Makefile
	@make -C ../PBFS/PBFS/src-fs

clean:
	@printf "$(CLR_RED) -- Cleaning Kernel Files$(CLR_RESET)\n"
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@printf "$(CLR_GREEN) -- Cleaned$(CLR_RESET)\n"
