PBFS_CLI := PBFS/PBFS/build-cli/pbfs-cli
DD := dd

BOOTLOADER := Bootloader
BOOTLOADER_BUILD := $(BOOTLOADER)/build
BOOTLOADER_BIN := $(BOOTLOADER)/bin

BOOTLOADER_STAGE1 := $(BOOTLOADER_BIN)/stage1.bin
BOOTLOADER_STAGE2 := $(BOOTLOADER_BIN)/stage2.bin

KERNEL := Kernel
KERNEL_BUILD := $(KERNEL)/build
KERNEL_BIN := $(KERNEL)/bin

AOS_KERNEL := $(KERNEL_BIN)/aos.bin

MAKE := make

BIN_DIR := Bin
BUILD_DIR := Build

DISK := $(BIN_DIR)/disk.pbfs

.PHONY: all clean

all: $(BUILD_DIR) $(BIN_DIR) $(DISK)

$(BUILD_DIR):
	@echo "Making $(BUILD_DIR)"
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@echo "Making $(BIN_DIR)"
	@mkdir -p $(BIN_DIR)

$(DISK): $(BOOTLOADER_STAGE1) $(BOOTLOADER_STAGE2) $(AOS_KERNEL)
	@echo "Creating AOS Disk..."
	$(PBFS_CLI) $(DISK) -bs 512 -tb 4096 -dn AOS_DISK -f
	$(DD) if=$(BOOTLOADER_STAGE1) of=$(DISK) bs=512 count=1 conv=notrunc
	$(DD) if=$(BOOTLOADER_STAGE2) of=$(DISK) bs=512 seek=4 conv=notrunc
	$(DD) if=$(AOS_KERNEL) of=$(DISK) bs=512 seek=16 conv=notrunc
	@echo "DONE!"

$(BOOTLOADER_STAGE1) $(BOOTLOADER_STAGE2):
	@echo "Creating AOS Bootloader..."
	$(MAKE) -C $(BOOTLOADER)

$(AOS_KERNEL):
	@echo "Creating AOS Kernel..."
	$(MAKE) -C $(KERNEL)

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
