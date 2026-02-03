CC := gcc
CFLAGS := -ffreestanding -m64 -fno-stack-protector -fno-pic -Wall -nostdlib -fno-pie -I ../PBFS/PBFS/headers -I ../Headers -I ./ -w

ASM := nasm
ASMFLAGS := -f elf64

LD := ld
LDFLAGS := -m elf_x86_64 -nostdlib --nmagic --omagic -L ../PBFS/PBFS/build-fs -T linker.ld
LDPBFS := ../PBFS/PBFS/build-fs/pbfs.a

OBJCOPY := objcopy

BUILD_DIR := build
BIN_DIR := bin
SRC_DIR := src

KERNEL := $(wildcard $(SRC_DIR)/*.c)
KERNEL_O := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL))
KERNEL_ELF := $(BUILD_DIR)/aos.elf
KERNEL_BIN := $(BIN_DIR)/aos.bin

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

all: $(BUILD_DIR) $(BIN_DIR) $(KERNEL_BIN)

$(BUILD_DIR):
	@echo "Creating $(BUILD_DIR) ..."
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@echo "Creating $(BIN_DIR) ..."
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling Kernel...."
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(DRIVERS_DIR)/%.c
	@echo "Compiling Drivers..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(MEMORY_DIR)/%.c
	@echo "Compiling Memory Manager..."
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(ASM_x86_64_DIR)/%.asm
	@echo "Assembling Assembly files..."
	$(ASM) $(ASMFLAGS) $< -o $@

$(KERNEL_ELF): $(KERNEL_O) $(DRIVERS_O) $(MEMORY_FILES_O) $(ASM_x86_64_FILES_O)
	@echo "Linking Kernel..."
	$(LD) $(LDFLAGS) $(KERNEL_O) $(MEMORY_FILES_O) $(ASM_x86_64_FILES_O) $(DRIVERS_O) $(LDPBFS) -o $(KERNEL_ELF)

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "Copying Kernel to binary..."
	$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
