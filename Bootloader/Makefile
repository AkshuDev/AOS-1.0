CC := gcc
OBJCOPY := objcopy
LD := ld
ASM := nasm

CFLAGS := -ffreestanding -mno-red-zone -mno-sse -O0 -fno-stack-protector -g -fno-pic -nostdlib -m64 -I ../PBFS/PBFS/headers -I ../PBFS/PBFS/PEFI/includes -I ../Headers -w
LDFLAGS := -m elf_x86_64 -T linker.ld -g
LDENDFLAGS := 
ASMFLAGS := -f elf32 -g
ASMFLAGS_BIN := -f bin

BUILD_DIR := build

PBFS_DISK_IO := ../PBFS/PBFS/tools/disk_io.asm 
PBFS_DISK_PARAMS := ../PBFS/PBFS/tools/disk_params.asm
PBFS_DISK_IO_O := $(BUILD_DIR)/disk_io.o
PBFS_DISK_PARAMS_O := $(BUILD_DIR)/disk_params.o

BIN_DIR := bin
SRC_DIR := src

stage1 := $(SRC_DIR)/stage1.asm
stage2 := $(SRC_DIR)/stage2.asm
stage3 := $(SRC_DIR)/stage3.c

stage3_o := $(BUILD_DIR)/stage3.o

stage3_elf := $(BUILD_DIR)/stage3.elf

stage1_bin := $(BIN_DIR)/stage1.bin
stage2_bin := $(BIN_DIR)/stage2.bin
stage3_bin := $(BIN_DIR)/stage3.bin

.PHONY: all clean

all: $(BUILD_DIR) $(BIN_DIR) $(stage1_bin) $(stage2_bin) $(stage3_bin)

$(BUILD_DIR):
	@echo "Creating $(BUILD_DIR) ..."
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@echo "Creating $(BIN_DIR) ..."
	@mkdir -p $(BIN_DIR)

$(stage1_bin): $(stage1)
	@echo "Assembling Stage 1 AOS Bootloader..."
	$(ASM) $(ASMFLAGS_BIN) $(stage1) -o $(stage1_bin)

$(stage3_bin): $(stage3_elf)
	@echo "Copying Stage 3 AOS Bootloader into binary..."
	$(OBJCOPY) -O binary $(stage3_elf) $(stage3_bin)

$(stage3_elf): $(stage3_o)
	@echo "Linking Stage 3 AOS Bootloader into elf..."
	$(LD) $(LDFLAGS) $(stage3_o) -o $(stage3_elf) $(LDENDFLAGS)

$(stage2_bin): $(stage2)
	@echo "Assembling Stage 2 AOS Bootloader into binary..."
	$(ASM) $(ASMFLAGS_BIN) $(stage2) -o $(stage2_bin)

$(stage3_o): $(stage3)
	@echo "Compiling Stage 3 AOS Bootloader..."
	$(CC) $(CFLAGS) -c $(stage3) -o $(stage3_o)

$(PBFS_DISK_PARAMS_O): $(PBFS_DISK_PARAMS)
	@echo "Assembling PBFS Tools..."
	$(ASM) $(ASMFLAGS) $(PBFS_DISK_PARAMS) -o $(PBFS_DISK_PARAMS_O)

$(PBFS_DISK_IO_O): $(PBFS_DISK_IO)
	@echo "Assembling PBFS Tools..."
	$(ASM) $(ASMFLAGS) $(PBFS_DISK_IO) -o $(PBFS_DISK_IO_O)

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
