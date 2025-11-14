#!/bin/bash

# KVM
# qemu-system-x86_64 \
#     -m 256M \
#     -cpu host \
#     -smp 2 \
#     -hda Bin/disk.pbfs \
#     -device virtio-gpu-pci,rombar=0 \
#     -vga virtio \
#     -net nic -net user \
#     -serial stdio \
#     -enable-kvm \
#     -d guest_errors,int \
#     -no-shutdown

# Non-KVM
qemu-system-x86_64 \
    -m 256M \
    -hda Bin/disk.pbfs \
    -device virtio-gpu-pci,rombar=0 \
    -vga virtio \
    -serial stdio \
    -d guest_errors,int \
    -no-shutdown \
    -no-reboot
