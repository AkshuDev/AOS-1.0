#!/bin/bash

mode=1
while [[ $# -gt 0 ]]; do
    case "$1" in
    -uefi)
        mode=0
        shift
        ;;
    -mbr)
        mode=1
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
1) #MBR
    make -C Bootloader && make -C Kernel && make
    ;;
0) # UEFI
    make -C Bootloader uefi && make -C Kernel && make uefi
    ;;
*)
    echo "Invalid Mode: $mode" >&2
    exit 1
    ;;
esac
