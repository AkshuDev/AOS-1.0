#!/bin/bash

mode=1
while [[ $# -gt 0 ]]; do
    case "$1" in
    -le | --low-end)
        mode=0
        shift
        ;;
    -he | --high-end)
        mode=1
        shift
        ;;
    -kvm | --kvm)
        mode=2
        shift
        ;;
    --)
        shift
        break
        ;;
    -*)
        echo "Unknown Option: $1" >&2
        exit 1
        ;;
    *)
        break
        ;;
    esac
done

case "$mode" in
2) #KVM
    qemu-system-x86_64 \
        -m 256M \
        -M q35,accel=kvm \
        -cpu host,migratable=no,+invtsc,+x2apic \
        -smp 16,sockets=1,cores=8,threads=2 \
        -drive file=Bin/disk.pbfs,format=raw,if=none,id=drive-ide0-0-0 \
        -device piix3-ide,id=ide \
        -device ide-hd,bus=ide.0,unit=0,drive=drive-ide0-0-0 \
        -device intel-iommu \
        -device virtio-gpu-pci,rombar=0 \
        -vga virtio \
        -net nic -net user \
        -serial stdio \
        -enable-kvm \
        -d guest_errors \
        -no-shutdown
    ;;
1) # Non-KVM High-End
    qemu-system-x86_64 \
        -m 256M \
        -M q35 \
        -cpu Haswell,-hle,-rtm,-pcid,-invpcid,-tsc-deadline,+x2apic,vendor=GenuineIntel \
        -smp 16,sockets=1,dies=1,cores=16,threads=1 \
        -drive file=Bin/disk.pbfs,format=raw,if=none,id=drive-ide0-0-0 \
        -device piix3-ide,id=ide \
        -device ide-hd,bus=ide.0,unit=0,drive=drive-ide0-0-0 \
        -device intel-iommu \
        -device virtio-gpu-pci,rombar=0 \
        -vga virtio \
        -serial stdio \
        -d guest_errors \
        -no-shutdown
    ;;
0) # Non-KVM Low-End
    qemu-system-x86_64 \
        -m 256M \
        -hda Bin/disk.pbfs \
        -device virtio-gpu-pci,rombar=0 \
        -vga virtio \
        -serial stdio \
        -d guest_errors \
        -no-shutdown \
        -no-reboot
    ;;
*)
    echo "Invalid Mode: $mode" >&2
    exit 1
    ;;
esac
