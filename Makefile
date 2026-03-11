PBFS_CLI := PBFS/PBFS/build-cli/pbfs-cli
DD := dd

BOOTLOADER := Bootloader
MBR_BOOTLOADER := $(BOOTLOADER)/MBR
UEFI_BOOTLOADER := $(BOOTLOADER)/UEFI

MBR_BOOTLOADER_BIN := $(MBR_BOOTLOADER)/bin
UEFI_BOOTLOADER_BIN := $(UEFI_BOOTLOADER)/bin

MBR_BOOTLOADER_STAGE1 := $(MBR_BOOTLOADER_BIN)/stage1.bin
MBR_BOOTLOADER_STAGE2 := $(MBR_BOOTLOADER_BIN)/stage2.bin
MBR_BOOTLOADER_STAGE3 := $(MBR_BOOTLOADER_BIN)/stage3.bin

UEFI_BOOTLOADER_EFI := $(UEFI_BOOTLOADER_BIN)/aos_bootloader.img

KERNEL := Kernel
KERNEL_BUILD := $(KERNEL)/build
KERNEL_BIN := $(KERNEL)/bin

AOS_KERNEL := $(KERNEL_BIN)/aos.bin

MAKE := make

BIN_DIR := Bin
BUILD_DIR := Build

DISK := $(BIN_DIR)/disk.pbfs

.PHONY: all clean init run build build_uefi build_mbr

all: $(BUILD_DIR) $(BIN_DIR) $(DISK)

$(BUILD_DIR):
	@echo "Making $(BUILD_DIR)"
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@echo "Making $(BIN_DIR)"
	@mkdir -p $(BIN_DIR)

$(DISK): $(MBR_BOOTLOADER_STAGE1) $(MBR_BOOTLOADER_STAGE2) $(MBR_BOOTLOADER_STAGE3) $(AOS_KERNEL)
	@echo "Creating+Formatting AOS Disk and adding Kernel..."
	$(PBFS_CLI) $(DISK) \
		-bs 512 -tb 16384 -dn AOS_DISK -rkt -rbp 1024 2048 \
		-c -f \
		--mbr -btl $(MBR_BOOTLOADER_STAGE1) \
		--permissions rw -ad /root \
		-k $(AOS_KERNEL) AOS++
	@echo "Filling in the Bootloader partition..."
	$(DD) if=$(MBR_BOOTLOADER_STAGE2) of=$(DISK) bs=512 seek=1024 conv=notrunc
	$(DD) if=$(MBR_BOOTLOADER_STAGE3) of=$(DISK) bs=512 seek=2048 conv=notrunc
	@echo "DONE!"

$(MBR_BOOTLOADER_STAGE1) $(MBR_BOOTLOADER_STAGE2) $(MBR_BOOTLOADER_STAGE3):
	@echo "Creating MBR AOS Bootloader..."
	$(MAKE) -C $(BOOTLOADER) mbr

$(UEFI_BOOTLOADER_EFI):
	@echo "Creating UEFI AOS Bootloader..."
	$(MAKE) -C $(BOOTLOADER) uefi

$(AOS_KERNEL):
	@echo "Creating AOS Kernel..."
	$(MAKE) -C $(KERNEL)

uefi: $(BUILD_DIR) $(BIN_DIR) $(UEFI_BOOTLOADER_EFI) $(AOS_KERNEL)
	@echo "Creating+Formatting AOS Disk and adding Kernel (UEFI Bootloader)..."
	$(PBFS_CLI) $(DISK) \
		-bs 512 -tb 16384 -dn AOS_DISK -rkt -rbp 1024 2048 \
		-c -f \
		--gpt -btl $(UEFI_BOOTLOADER_EFI) \
		--permissions rw -ad /root \
		-k $(AOS_KERNEL) AOS++
	@echo "DONE!"

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

init:
	@echo "Initializing..."
	@chmod +x ./init.sh
	@chmod +x ./run.sh
	@chmod +x ./build.sh
	@./init.sh

run:
	@echo "Running"
	@./run.sh

build:
	@echo "Building MBR..."
	@./build.sh -mbr

build_uefi:
	@echo "Building UEFI..."
	@./build.sh -uefi

build_mbr:
	@echo "Building MBR..."
	@./build.sh -mbr
