CC := gcc
OBJCOPY := objcopy
LD := ld
ASM := nasm
DD := dd

UEFI_CC = x86_64-w64-mingw32-gcc
MFORMAT = mformat
MCOPY = mcopy
UEFI_AR := x86_64-w64-mingw32-ar

MBR_DIR := MBR
UEFI_DIR := UEFI

MBR_BUILD_DIR := $(MBR_DIR)/build
MBR_BIN_DIR := $(MBR_DIR)/bin
MBR_SRC_DIR := $(MBR_DIR)/src
UEFI_BUILD_DIR := $(UEFI_DIR)/build
UEFI_BIN_DIR := $(UEFI_DIR)/bin
UEFI_SRC_DIR := $(UEFI_DIR)/src

CFLAGS := -ffreestanding -mno-red-zone -O0 -fno-stack-protector -fno-pic -nostdlib -m64 -I ../PBFS -I ../Headers -I ../Kernel -I MBR/inc -w
UEFI_CFLAGS := -mabi=ms \
		-ffreestanding -fshort-wchar \
		-mno-red-zone -fno-stack-protector \
		-g -O0 \
		-nostdlib -w -shared \
		-I ../PBFS -I ../Headers -I ../Kernel -I ../PBFS/PBFS/PEFI/includes -I UEFI/inc

LDFLAGS_UEFI = -shared \
        -Wl,--subsystem,10 \
        -Wl,--image-base,0x140000000 \
		-Wl,-q \
        -Wl,--section-alignment,0x1000 \
        -Wl,--file-alignment,0x1000 \
        -Wl,--entry,efi_main \
        -Wl,--enable-reloc-section \
        -Wl,--disable-stdcall-fixup \
		-Wl,--dll \
        -L $(UEFI_BUILD_DIR) -L ../PBFS/PBFS/PEFI/build

LDFLAGS_MBR := -m elf_x86_64 -T linker.ld -L ../PBFS/PBFS/build-fs
LDENDFLAGS := -lpbfs-ndrivers
LDENDFLAGS_UEFI := -lpbfs-ndrivers -lpefi

ASMFLAGS_MBR := -f elf32 -g
ASMFLAGS_MBR_BIN := -f bin

UEFI_BOOTLOADER_SRC := $(UEFI_SRC_DIR)/bootloader_uefi.c $(UEFI_SRC_DIR)/stdlib_freestanding.c
UEFI_BOOTLOADER_EFI := $(UEFI_BUILD_DIR)/bootloader_uefi.efi
UEFI_TARGET := $(UEFI_BIN_DIR)/aos_bootloader.img

KERNEL := ../Kernel
DRIVER := $(KERNEL)/drivers
DRIVER_CORE := $(DRIVER)/core
MBR_DRIVERS_SRC := $(DRIVER)/io/io.c
MBR_BTL_DRIVERS_SRC := $(MBR_SRC_DIR)/pcie.c \
					$(MBR_SRC_DIR)/acpi.c \
					$(MBR_SRC_DIR)/drive.c \
					$(MBR_SRC_DIR)/drivers/sata.c \
					$(MBR_SRC_DIR)/kfuncs.c \
					$(MBR_SRC_DIR)/ambrc.c
MBR_DRIVERS_O := $(patsubst $(KERNEL)/%.c,$(MBR_BUILD_DIR)/%.o,$(MBR_DRIVERS_SRC))
MBR_BTL_DRIVERS_O := $(patsubst $(MBR_SRC_DIR)/%.c,$(MBR_BUILD_DIR)/%.o,$(MBR_BTL_DRIVERS_SRC))

MBR_stage1 := $(MBR_SRC_DIR)/stage1.asm
MBR_stage2 := $(MBR_SRC_DIR)/stage2.asm
MBR_stage3 := $(MBR_SRC_DIR)/stage3.c

MBR_stage3_o := $(MBR_BUILD_DIR)/stage3.o
MBR_stage3_elf := $(MBR_BUILD_DIR)/stage3.elf

MBR_stage1_bin := $(MBR_BIN_DIR)/stage1.bin
MBR_stage2_bin := $(MBR_BIN_DIR)/stage2.bin
MBR_stage3_bin := $(MBR_BIN_DIR)/stage3.bin

.PHONY: all clean uefi mbr

all: $(MBR_BUILD_DIR) $(MBR_BIN_DIR) $(MBR_stage1_bin) $(MBR_stage2_bin) $(MBR_stage3_bin)

$(MBR_BUILD_DIR):
	@echo "Creating $(MBR_BUILD_DIR) ..."
	@mkdir -p $(MBR_BUILD_DIR)

$(MBR_BIN_DIR):
	@echo "Creating $(MBR_BIN_DIR) ..."
	@mkdir -p $(MBR_BIN_DIR)

$(UEFI_BUILD_DIR):
	@echo "Creating $(UEFI_BUILD_DIR) ..."
	@mkdir -p $(UEFI_BUILD_DIR)

$(UEFI_BIN_DIR):
	@echo "Creating $(UEFI_BIN_DIR) ..."
	@mkdir -p $(UEFI_BIN_DIR)

$(MBR_stage1_bin): $(MBR_stage1)
	@echo "Assembling MBR Stage 1 AOS Bootloader..."
	$(ASM) $(ASMFLAGS_MBR_BIN) $(MBR_stage1) -o $(MBR_stage1_bin)

$(MBR_stage3_bin): $(MBR_stage3_elf)
	@echo "Copying Stage 3 AOS Bootloader into binary..."
	$(OBJCOPY) -O binary $(MBR_stage3_elf) $(MBR_stage3_bin)

$(MBR_BUILD_DIR)/%.o: $(KERNEL)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(MBR_BUILD_DIR)/%.o: $(MBR_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(MBR_stage3_elf): $(MBR_stage3_o) $(MBR_DRIVERS_O) $(MBR_BTL_DRIVERS_O)
	@echo "Linking MBR Stage 3 AOS Bootloader into elf..."
	$(LD) $(LDFLAGS_MBR) $(MBR_stage3_o) $(MBR_DRIVERS_O) $(MBR_BTL_DRIVERS_O) -o $(MBR_stage3_elf) $(LDENDFLAGS)

$(MBR_stage2_bin): $(MBR_stage2)
	@echo "Assembling MBR Stage 2 AOS Bootloader into binary..."
	$(ASM) $(ASMFLAGS_MBR_BIN) $(MBR_stage2) -o $(MBR_stage2_bin)

$(MBR_stage3_o): $(MBR_stage3)
	@echo "Compiling MBR Stage 3 AOS Bootloader..."
	$(CC) $(CFLAGS) -c $(MBR_stage3) -o $(MBR_stage3_o)

$(UEFI_TARGET): $(UEFI_BOOTLOADER_EFI)
	@echo "Compiling everything to a FAT32 Image"
	$(DD) if=/dev/zero of=$(UEFI_TARGET) bs=512 count=2048
	$(MFORMAT) -i $(UEFI_TARGET) -v "ESP" -C -t 80 -h 2 -s 18 ::
	mmd -i $(UEFI_TARGET) ::/EFI
	mmd -i $(UEFI_TARGET) ::/EFI/BOOT
	$(MCOPY) -i $(UEFI_TARGET) $(UEFI_BOOTLOADER_EFI) ::/EFI/BOOT/BOOTX64.EFI

$(UEFI_BOOTLOADER_EFI): $(UEFI_BOOTLOADER_SRC)
	@echo "Recomping PBFS for PE32..."
	$(UEFI_CC) $(UEFI_CFLAGS) -c -I ../PBFS/PBFS/headers ../PBFS/PBFS/src-fs/pbfs-fs-ndrivers.c -o $(UEFI_BUILD_DIR)/pbfs-fs-ndrivers.o
	$(UEFI_CC) $(UEFI_CFLAGS) -c -I ../PBFS/PBFS/headers ../PBFS/PBFS/tools/utils.c -o $(UEFI_BUILD_DIR)/utils.o
	$(UEFI_AR) rcs $(UEFI_BUILD_DIR)/libpbfs-ndrivers.a $(UEFI_BUILD_DIR)/utils.o $(UEFI_BUILD_DIR)/pbfs-fs-ndrivers.o
	@echo "Compiling UEFI AOS Bootloader to EFI..."
	$(UEFI_CC) $(UEFI_CFLAGS) -o $(UEFI_BOOTLOADER_EFI) $(UEFI_BOOTLOADER_SRC) $(LDFLAGS_UEFI) $(LDENDFLAGS_UEFI)

uefi: $(UEFI_BUILD_DIR) $(UEFI_BIN_DIR) $(UEFI_TARGET)
mbr: $(MBR_BUILD_DIR) $(MBR_BIN_DIR) $(MBR_stage1_bin) $(MBR_stage2_bin) $(MBR_stage3_bin)

clean:
	@echo "Cleaning..."
	@rm -rf $(MBR_BUILD_DIR) $(MBR_BIN_DIR)
	@rm -rf $(UEFI_BUILD_DIR) $(UEFI_BIN_DIR)
