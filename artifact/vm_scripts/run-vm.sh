#! /usr/bin/sudo /bin/bash
set -e

MEM_FRAC=0.5
MEM=$(cat /proc/meminfo | grep MemTotal | awk '$3=="kB"{$2='$MEM_FRAC'*$2/1024;$3=""} 1' | tr -d " " | cut -d ":" -f 2 | cut -d "." -f 1)

# VM_DIR=$(realpath /home/iangneal/workspace/vms/)
VM_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
IMG_NAME=$VM_DIR/agamotto.qcow2

exec qemu-system-x86_64 \
          -kernel $VM_DIR/boot/vmlinuz \
          -initrd $VM_DIR/boot/initrd.img \
          -nographic \
          -enable-kvm \
          -hda $IMG_NAME \
          -m $MEM \
          --append "console=ttyS0,115200n8, root=/dev/sda1" \
          -balloon virtio \
          -smp $(nproc) \
          -cpu host,$CPU_FEATURES_DISABLE \
          -net user,hostfwd=tcp::5000-:22 \
          -net nic 

          #-netdev user,id=mynet \
          #-device virtio-net-pci,netdev=mynet \
          #-nographic -no-reboot \
