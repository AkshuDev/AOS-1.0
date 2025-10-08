CC := gcc
OBJCOPY := objcopy
LD := ld
ASM := nasm

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -nostdlib -m32 -I ../PBFS/PBFS/headers -I ../PBFS/PBFS/PEFI/includes
LDFLAGS := -m elf_i386 -Ttext 0x1000
ASMFLAGS := -f elf32
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

stage2_o := $(BUILD_DIR)/stage2.o
stage3_o := $(BUILD_DIR)/stage3.o

stage2_elf := $(BUILD_DIR)/stage2.elf

stage1_bin := $(BIN_DIR)/stage1.bin
stage2_bin := $(BIN_DIR)/stage2.bin # stage 3 and stage2 are linked together

.PHONY: all clean

all: $(BUILD_DIR) $(BIN_DIR) $(stage1_bin) $(stage2_bin)

$(BUILD_DIR):
	@echo "Creating $(BUILD_DIR) ..."
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@echo "Creating $(BIN_DIR) ..."
	@mkdir -p $(BIN_DIR)

$(stage1_bin): $(stage1)
	@echo "Assembling Stage 1 AOS Bootloader..."
	$(ASM) $(ASMFLAGS_BIN) $(stage1) -o $(stage1_bin)

$(stage2_bin): $(stage2_elf) 
	@echo "Copying Stage 2 + Stage 3 AOS Bootloader into binary..."
	$(OBJCOPY) -O binary $(stage2_elf) $(stage2_bin)

$(stage2_elf): $(stage2_o) $(stage3_o) $(PBFS_DISK_IO_O) $(PBFS_DISK_PARAMS_O)
	@echo "Linking Stage 2 and Stage 3 AOS Bootloader into elf..."
	$(LD) $(LDFLAGS) $(stage2_o) $(stage3_o) $(PBFS_DISK_IO_O) $(PBFS_DISK_PARAMS_O) -o $(stage2_elf)

$(stage2_o): $(stage2)
	@echo "Assembling Stage 2 AOS Bootloader..."
	$(ASM) $(ASMFLAGS) $(stage2) -o $(stage2_o)

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
