#!/bin/bash

# KVM
# qemu-system-x86_64 \
#     -m 256M \
#     -M q35,accel=kvm \
#     -cpu host,migratable=no,+invtsc,+x2apic \
#     -smp 16,sockets=1,cores=8,threads=2 \
#     -drive file=Bin/disk.pbfs,format=raw,if=none,id=drive-ide0-0-0 \
#     -device piix3-ide,id=ide \
#     -device ide-hd,bus=ide.0,unit=0,drive=drive-ide0-0-0 \
#     -device intel-iommu \
#     -device virtio-gpu-pci,rombar=0 \
#     -vga virtio \
#     -net nic -net user \
#     -serial stdio \
#     -enable-kvm \
#     -d guest_errors,int \
#     -no-shutdown

# Non-KVM High-End
qemu-system-x86_64 \
    -m 256M \
    -M q35 \
    -cpu max \
    -smp 16,sockets=1,cores=8,threads=2 \
    -drive file=Bin/disk.pbfs,format=raw,if=none,id=drive-ide0-0-0 \
    -device piix3-ide,id=ide \
    -device ide-hd,bus=ide.0,unit=0,drive=drive-ide0-0-0 \
    -device intel-iommu \
    -device virtio-gpu-pci,rombar=0 \
    -vga virtio \
    -serial stdio \
    -d guest_errors,int \
    -no-shutdown

# Non-KVM Low-End
# qemu-system-x86_64 \
#     -m 256M \
#     -hda Bin/disk.pbfs \
#     -device virtio-gpu-pci,rombar=0 \
#     -vga virtio \
#     -serial stdio \
#     -d guest_errors,int \
#     -no-shutdown \
#     -no-reboot
