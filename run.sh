#!/bin/bash

# AOS++/AOS-1.0 Run script for ease of use


show_help() {
    cat << EOF
    Usage: ./run.sh [OPTIONS]

    Modes:
        -le, --low-end                 Low-end emulation
        -he, --high-end                High-end emulation (default)
        -kvm, --kvm                    Enable KVM acceleration

    CPU Options:
        -cpu=<type>, --cpu-type=<type> Select CPU type -
                                        intel | amd | intel-server | amd-server
        -arch=<arch>, --architecture=<arch> Select CPU Architecture -
                                        x86_64 | ia32 | aarch64

    GPU Options:
        -gpu, --gpu-type=<type>        Select GPU -
                                        virtio | nvidia | amd | vmware | bochs

        -ega, --enable-gpu-acceleration
                                        Enable VirGL/OpenGL GPU acceleration

    Input Devices:
        -kbd=<type>, --keyboard-type=<type>   Keyboard type: usb | ps2
        -mouse, --mouse                       Enable USB mouse

    Memory:
        -ram=<size>, --max-ram=<size>           Set RAM (e.g., 512M, 2G)
		-bdev=<device>, --boot-device=<device>  Specify Boot Storage Device from -
													sata, nvme, ide, usb
		-adev=<device>, --add-device=<device>   Specify Extra Device from -
													sata, nvme, ide, usb, xhci, ehci, uhci, scsi,
													virtio, floppy, cdrom, sd, mtd, pflash, sas, iscsi

    Logging:
        -log=a,b,c, --logs=a,b,c       QEMU debug logs (comma-separated)

    Display:
        -nog, --nographics             Disable graphical output (headless)

    Networking:
        -web, --internet               Enable user-mode networking

    Firmware:
        -uefi, --uefi                  Enable UEFI (OVMF)

    Other:
        -er, --enable-reboot           Allow rebooting
        -h, --help                     Show this help message
        -ser=<out>, --serial=<out>     Redirect Serial Output to either 'file:/path/to/file' or 'stdio'
        -fout=<file>, --file-out        Redirect all output to a file

    Examples:
    ./run.sh --high-end --cpu=amd --ram=2G --gpu=virtio --web
    ./run.sh -kvm --cpu=intel-server --nographics --logs=guest_errors,int
EOF
}

mode=1
cpu_type="intel"
gpu_type="virtio"
gpu_accel=0
kbd_type="ps2"
mouse_enabled=0
ram="256M"
logs="guest_errors"
nographics=0
internet=0
enable_reboot=0
arch="x86_64"
serial="stdio"
file_out="none"
uefi=0
boot_device="sata"
extra_devices=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --low-end|-le) mode=0 ;;
        --high-end|-he) mode=1 ;;
        --kvm|-kvm) mode=2 ;;
        --cpu-type=*|-cpu=*) cpu_type="${1#*=}" ;;
        --gpu-type=*|-gpu=*) gpu_type="${1#*=}" ;;
        --enable-gpu-acceleration|-ega) gpu_accel=1 ;;
        --keyboard-type=*|-kbd=*) kbd_type="${1#*=}" ;;
        --mouse|-mouse) mouse_enabled=1 ;;
        --max-ram=*|-ram=*) ram="${1#*=}" ;;
		--boot-device=*|-bdev=*) boot_device="${1#*=}" ;;
		--add-device=*|-adev=*) extra_devices+=("${1#*=}") ;;
        --logs=*|-log=*) logs="${1#*=}" ;;
        --use-uefi|-uefi) uefi=1 ;;
        --architecture=*|-arch=*) arch="${1#*=}" ;;
        --nographics|-nog) nographics=1 ;;
        --internet|-web) internet=1 ;;
        --enable-reboot|-er) enable_reboot=1 ;;
        --serial=*|-ser=*) serial="${1#*=}" ;;
        --file-out=*|-fout=*) file_out="${1#*=}" ;;
        --help|-h)
            show_help
            exit 0
            ;;
        --) shift; break ;;
        *) echo "Unknown Option: $1" >&2; exit 1 ;;
    esac
    shift
done

QEMU="qemu-system-x86_64"

if [[ "$arch" == "x86_64" ]]; then
    QEMU="qemu-system-x86_64"
elif [[ "$arch" == "ia32" ]]; then
    QEMU="qemu-system-i386"
elif [[ "$arch" == "aarch64" ]]; then
    QEMU="qemu-system-aarch64"
else
    QEMU="qemu-system-x86_64"
fi

get_cpu() {
    local cpu_model=""
    local cpu_vendor=""
    local cpu_model_fallback=""

    case "$cpu_type" in
        intel)
            cpu_model="AlderLake-Client"
            cpu_vendor="GenuineIntel"
            cpu_model_fallback="Haswell"
            ;;
        amd)
            cpu_model="Zen5"
            cpu_vendor="AuthenticAMD"
            cpu_model_fallback="EPYC"
            ;;
        intel-server)
            cpu_model="SapphireRapids"
            cpu_vendor="GenuineIntel"
            cpu_model_fallback="Skylake-Server"
            ;;
        amd-server)
            cpu_model="EPYC-Genoa"
            cpu_vendor="AuthenticAMD"
            cpu_model_fallback="EPYC"
            ;;
        *)
            echo "[Warning] Unsupported cpu passed, using qemu64" >&2
            echo "qemu64"
            return
            ;;
    esac

    if qemu-system-x86_64 -cpu help | grep -q "$cpu_model"; then
        echo "$cpu_model,vendor=$cpu_vendor"
    else
        echo "[Warning] Unsupported cpu, using closest fallback - $cpu_model_fallback" >&2
        echo "$cpu_model_fallback,vendor=$cpu_vendor"
    fi
}

get_smp() {
    case "$cpu_type" in
        intel|amd) echo "-smp 16" ;;
        intel-server|amd-server) echo "-smp 32" ;;
        *)
            echo "[Warning] Unsupported cpu passed, using 4 Cores SMP" >&2
            echo "-smp 4" ;;
    esac
}

get_gpu() {
    case "$gpu_type" in
        virtio)
            if [[ $gpu_accel -eq 1 ]]; then
                echo "-device virtio-vga-gl -display gtk,gl=on -vga none"
            else
                echo "-device virtio-vga -display gtk -vga none"
            fi
            ;;
        vmware)
            echo "-vga vmware"
            ;;
        bochs)
            echo "-vga std"
            ;;
        *)
            echo "[Warning] Unsupported gpu passed, using STD (Bochs)" >&2
            echo "-vga std"
            ;;
    esac
}

get_keyboard() {
    case "$kbd_type" in
        usb) echo "-device qemu-xhci -device usb-kbd" ;;
        ps2) echo "" ;;
        *)
            echo "[Warning] Unsupported keyboard passed, using PS2" >&2
            echo "" ;;
    esac
}

get_mouse() {
    if [[ $mouse_enabled -eq 1 ]]; then
        echo "-device usb-mouse"
    fi
}

get_network() {
    if [[ $internet -eq 1 ]]; then
        echo "-net nic -net user"
    fi
}

get_display() {
    if [[ $nographics -eq 1 ]]; then
        echo "-nographic"
    fi
}

get_extra_options() {
    if [[ $enable_reboot -ne 1 ]]; then
        echo "-no-shutdown -no-reboot"
    else
        echo "-no-shutdown"
    fi
}

get_file_options() {
    case "$file_out" in
        none)
            echo ""
            ;;
        *)
            echo "> $file_out 2>&1"
            ;;
    esac
}

append_uefi_bios() {
    if [[ $uefi -eq 1 ]]; then
        echo "-global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd,readonly=on \
        -drive if=pflash,format=raw,unit=1,file=./virtualization/ovmf_vars.4m.fd"
    fi
}

declare -A CONTROLLERS
declare -A BUS_NAMES

ensure_controller() {
    local type="$1"

    if [[ -z "${CONTROLLERS[$type]}" ]]; then
        CONTROLLERS[$type]=1

        case "$type" in
            ahci)
                CONTROLLER_OPTS+=" -device ahci,id=ahci"
                ;;
            xhci)
                CONTROLLER_OPTS+=" -device qemu-xhci,id=xhci"
                ;;
            scsi)
                CONTROLLER_OPTS+=" -device virtio-scsi-pci,id=scsi"
                ;;
            sas)
                CONTROLLER_OPTS+=" -device megasas,id=sas"
                ;;
        esac
    fi
}

get_device() {
    local devtype="$1"
    local id="$2"
    local img="$3"

    case "$devtype" in
        sata)
            ensure_controller ahci
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=none,id=drive$id"
            STORAGE_OPTS+=" -device ide-hd,bus=ahci.0,drive=drive$id"
            ;;

        nvme)
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=none,id=drive$id"
            STORAGE_OPTS+=" -device nvme,serial=deadbeef$id,drive=drive$id"
            ;;

        usb|xhci)
            ensure_controller xhci
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=none,id=drive$id"
            STORAGE_OPTS+=" -device usb-storage,bus=xhci.0,drive=drive$id"
            ;;

        ehci)
            ensure_controller ehci
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=none,id=drive$id"
            STORAGE_OPTS+=" -device usb-storage,bus=ehci.0,drive=drive$id"
            ;;

        uhci)
            ensure_controller uhci
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=none,id=drive$id"
            STORAGE_OPTS+=" -device usb-storage,bus=uhci.0,drive=drive$id"
            ;;

        scsi)
            ensure_controller scsi
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=none,id=drive$id"
            STORAGE_OPTS+=" -device scsi-hd,bus=scsi.0,drive=drive$id"
            ;;

        sas)
            ensure_controller sas
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=none,id=drive$id"
            STORAGE_OPTS+=" -device scsi-hd,bus=sas.0,drive=drive$id"
            ;;

        virtio)
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=none,id=drive$id"
            STORAGE_OPTS+=" -device virtio-blk-pci,drive=drive$id"
            ;;

        ide)
            STORAGE_OPTS+=" -drive file=$img,format=raw,index=$id,media=disk"
            ;;

        floppy)
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=floppy,index=$id"
            ;;

        cdrom)
            STORAGE_OPTS+=" -drive file=$img,media=cdrom,index=$id"
            ;;

        sd)
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=sd"
            ;;

        mtd)
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=mtd"
            ;;

        pflash)
            STORAGE_OPTS+=" -drive file=$img,format=raw,if=pflash"
            ;;

        iscsi)
            STORAGE_OPTS+=" -drive file=iscsi:$img"
            ;;

        *)
            echo "[Warning] Unsupported device type '$devtype'" >&2
            ;;
    esac
}

CPU_OPTS="-cpu $(get_cpu)"
SMP_OPTS="$(get_smp)"
GPU_OPTS="$(get_gpu)"
KBD_OPTS="$(get_keyboard)"
MOUSE_OPTS="$(get_mouse)"
NET_OPTS="$(get_network)"
DISPLAY_OPTS="$(get_display)"
LOG_OPTS="-d $logs"
EXTRA_OPTS="$(get_extra_options)"
SERIAL_OPTS="-serial $serial"
FILE_OPTS="$(get_file_options)"
UEFI_OPTS="$(append_uefi_bios)"
STORAGE_OPTS=""
CONTROLLER_OPTS=""

get_device "$boot_device" 0 "Bin/disk.pbfs"

devnum=1
for dev in "${extra_devices[@]}"; do
    get_device "$dev" "$devnum" "Bin/disk${devnum}.pbfs"
    ((devnum++))
done

devnum=1
for dev in "${extra_devices[@]}"; do
    STORAGE_OPTS+=" $(get_device "$dev" "$devnum" "Bin/disk${devnum}.pbfs")"
    ((devnum++))
done

case "$mode" in
    2) # KVM
        eval "$QEMU \
            -m $ram \
            -M q35,accel=kvm \
            $CPU_OPTS \
            $SMP_OPTS \
            -enable-kvm \
			$CONTROLLER_OPTS \
            $STORAGE_OPTS \
            $GPU_OPTS \
            $KBD_OPTS \
            $MOUSE_OPTS \
            $NET_OPTS \
            $DISPLAY_OPTS \
            $SERIAL_OPTS \
            $LOG_OPTS \
            $EXTRA_OPTS \
            $UEFI_OPTS $FILE_OPTS"
        ;;

    1) # High-End (non-KVM)
        eval "$QEMU \
            -m $ram \
            -M q35 \
            $CPU_OPTS \
            $SMP_OPTS \
			$CONTROLLER_OPTS \
            $STORAGE_OPTS \
            $GPU_OPTS \
            $KBD_OPTS \
            $MOUSE_OPTS \
            $NET_OPTS \
            $DISPLAY_OPTS \
            $SERIAL_OPTS \
            $LOG_OPTS \
            $EXTRA_OPTS \
            $UEFI_OPTS $FILE_OPTS"
        ;;

    0) # Low-End
        eval "$QEMU \
            -m $ram \
            $CPU_OPTS \
            $SMP_OPTS \
			$CONTROLLER_OPTS \
           	$STORAGE_OPTS \
            $GPU_OPTS \
            $KBD_OPTS \
            $MOUSE_OPTS \
            $NET_OPTS \
            $DISPLAY_OPTS \
            $SERIAL_OPTS \
            $LOG_OPTS \
            $EXTRA_OPTS \
            $UEFI_OPTS $FILE_OPTS"
        ;;

    *)
        echo "Invalid Mode: $mode" >&2
        exit 1
        ;;
esac