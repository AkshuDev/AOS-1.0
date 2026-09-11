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

AOS_KERNEL := $(KERNEL_BIN)/aos.elf

MAKE := make

BIN_DIR := Bin
BUILD_DIR := Build

DISK := $(BIN_DIR)/disk.pbfs
TOTAL_DISK_BLOCKS := 32768

FSROOT_DIR := FSRoot

RUN_ARGS ?=

.PHONY: all clean init run build build_uefi build_mbr

all: $(BUILD_DIR) $(BIN_DIR) $(DISK)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(DISK): $(MBR_BOOTLOADER_STAGE1) $(MBR_BOOTLOADER_STAGE2) $(MBR_BOOTLOADER_STAGE3) $(AOS_KERNEL)
	@printf "$(CLR_YELLOW)Creating Final MBR Disk:$(CLR_RESET)\n"
	@printf "$(CLR_YELLOW) -- Creating+Formatting AOS Disk and adding Kernel...$(CLR_RESET)\n"
	@$(PBFS_CLI) $(DISK) \
		-bs 512 -tb $(TOTAL_DISK_BLOCKS) -dn AOS_DISK -rkt -rbp 1024 2048 \
		-c -f \
		--mbr -btl $(MBR_BOOTLOADER_STAGE1) \
		-k $(AOS_KERNEL) AOS++

	@printf "$(CLR_YELLOW) -- Adding FS ROOT to AOS Disk...$(CLR_RESET)\n"
	@find "$(FSROOT_DIR)" -mindepth 1 -type d -print0 | while IFS= read -r -d '' item; do \
		path="/$${item#$(FSROOT_DIR)/}"; \
		echo "Adding Folder: $$item to $$path"; \
		$(PBFS_CLI) $(DISK) -bs 512 -tb $(TOTAL_DISK_BLOCKS) -dn AOS_DISK --type dir --permissions rws -ad "$$path"; \
	done; \
	@find "$(FSROOT_DIR)" -type f -print0 | while IFS= read -r -d '' item; do \
		path="/$${item#$(FSROOT_DIR)/}"; \
		echo "Adding File: $$item as $$path"; \
		$(PBFS_CLI) $(DISK) -bs 512 -tb $(TOTAL_DISK_BLOCKS) -dn AOS_DISK --type file --permissions rs --name "$$path" -a "$$item"; \
	done

	@printf "$(CLR_YELLOW) -- Filling in the Bootloader partition...$(CLR_RESET)\n"
	@$(DD) if=$(MBR_BOOTLOADER_STAGE2) of=$(DISK) bs=512 seek=1024 conv=notrunc
	@$(DD) if=$(MBR_BOOTLOADER_STAGE3) of=$(DISK) bs=512 seek=2048 conv=notrunc
	
	@printf "$(CLR_GREEN)Disk Created Successfully.$(CLR_RESET)\n"

$(MBR_BOOTLOADER_STAGE1) $(MBR_BOOTLOADER_STAGE2) $(MBR_BOOTLOADER_STAGE3):
	$(MAKE) -C $(BOOTLOADER) mbr

$(UEFI_BOOTLOADER_EFI):
	$(MAKE) -C $(BOOTLOADER) uefi

$(AOS_KERNEL):
	$(MAKE) -C $(KERNEL)

uefi: $(BUILD_DIR) $(BIN_DIR) $(UEFI_BOOTLOADER_EFI) $(AOS_KERNEL)
	@printf "$(CLR_YELLOW)Creating Final UEFI Disk:$(CLR_RESET)\n"
	@printf "$(CLR_YELLOW) -- Creating+Formatting AOS Disk and adding Kernel...$(CLR_RESET)\n"
	
	@$(PBFS_CLI) $(DISK) \
		-bs 512 -tb $(TOTAL_DISK_BLOCKS) -dn AOS_DISK -rkt -rbp 1024 2048 \
		-c -f \
		--gpt -btl $(UEFI_BOOTLOADER_EFI) \
		--permissions rw \
		-k $(AOS_KERNEL) AOS++
	@printf "$(CLR_YELLOW) -- Adding FS ROOT to AOS Disk...$(CLR_RESET)\n"
	@find "$(FSROOT_DIR)" -mindepth 1 -type d -print0 | while IFS= read -r -d '' item; do \
		path="/$${item#$(FSROOT_DIR)/}"; \
		echo "Adding Folder: $$item to $$path"; \
		$(PBFS_CLI) $(DISK) -bs 512 -tb $(TOTAL_DISK_BLOCKS) -dn AOS_DISK --type dir --permissions rws -ad "$$path"; \
	done; \
	find "$(FSROOT_DIR)" -type f -print0 | while IFS= read -r -d '' item; do \
		path="/$${item#$(FSROOT_DIR)/}"; \
		echo "Adding File: $$item as $$path"; \
		$(PBFS_CLI) $(DISK) -bs 512 -tb $(TOTAL_DISK_BLOCKS) -dn AOS_DISK --type file --permissions rs --name "$$path" -a "$$item"; \
	done

	@printf "$(CLR_GREEN)Disk Created Successfully.$(CLR_RESET)\n"

clean:
	@printf "$(CLR_YELLOW)Cleaning...$(CLR_RESET)\n"
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@printf "$(CLR_GREEN)Cleaned.$(CLR_RESET)\n"

init:
	@printf "$(CLR_YELLOW)Initializing...$(CLR_RESET)\n"
	@chmod +x ./init.sh
	@chmod +x ./run.sh
	@chmod +x ./build.sh
	@./init.sh
	@printf "$(CLR_GREEN)Initialized.$(CLR_RESET)\n"

run:
	@printf "$(CLR_YELLOW)Running...$(CLR_RESET)\n"
	@./run.sh $(RUN_ARGS)
	@printf "$(CLR_GREEN)Run Completed.$(CLR_RESET)\n"

build:
	@./build.sh -mbr

build_uefi:
	@./build.sh -uefi

build_mbr:
	@./build.sh -mbr
