CC := gcc
CFLAGS := -ffreestanding -m32 -fno-stack-protector -fno-pic -Wall -nostdlib -fno-pie -I ../PBFS/PBFS/headers

LD := ld
LDFLAGS := -m elf_i386 -nostdlib --nmagic --omagic -L ../PBFS/PBFS/build-fs -T linker.ld
LDPBFS := ../PBFS/PBFS/build-fs/pbfs.a

OBJCOPY := objcopy

BUILD_DIR := build
BIN_DIR := bin
SRC_DIR := src

KERNEL := $(SRC_DIR)/aos.c
KERNEL_O := $(BUILD_DIR)/aos.o
KERNEL_ELF := $(BUILD_DIR)/aos.elf
KERNEL_BIN := $(BIN_DIR)/aos.bin

.PHONY: all clean

all: $(BUILD_DIR) $(BIN_DIR) $(KERNEL_BIN)

$(BUILD_DIR):
	@echo "Creating $(BUILD_DIR) ..."
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@echo "Creating $(BIN_DIR) ..."
	@mkdir -p $(BIN_DIR)

$(KERNEL_O): $(KERNEL)
	@echo "Compiling Kernel...."
	$(CC) $(CFLAGS) -c $(KERNEL) -o $(KERNEL_O)

$(KERNEL_ELF): $(KERNEL_O)
	@echo "Linking Kernel..."
	$(LD) $(LDFLAGS) $(KERNEL_O) $(LDPBFS) -o $(KERNEL_ELF)

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "Copying Kernel to binary..."
	$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
