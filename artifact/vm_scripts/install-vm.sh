#! /usr/bin/sudo /bin/bash
set -e

VM_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

UBUNTU_VERSION=20.04.1
UBUNTU_ISO=./iso/ubuntu-$UBUNTU_VERSION-live-server-arm64.iso
UBUNTU_URL=http://cdimage.ubuntu.com/releases/$UBUNTU_VERSION/release/ubuntu-$UBUNTU_VERSION-live-server-arm64.iso

KERNEL=./boot/linux
KERNEL_URL=http://archive.ubuntu.com/ubuntu/dists/bionic-updates/main/installer-amd64/current/images/netboot/ubuntu-installer/amd64/linux
INITRD=./boot/initrd.gz
INITRD_URL=http://archive.ubuntu.com/ubuntu/dists/bionic-updates/main/installer-amd64/current/images/netboot/ubuntu-installer/amd64/initrd.gz

mkdir -p boot
if [ ! -f $KERNEL ]; then
  wget $KERNEL_URL -O $KERNEL
fi
if [ ! -f $INITRD ]; then
  wget $INITRD_URL -O $INITRD
fi

MEM_FRAC=0.5
MEM=$(cat /proc/meminfo | grep MemTotal | awk '$3=="kB"{$2='$MEM_FRAC'*$2/1024;$3=""} 1' | tr -d " " | cut -d ":" -f 2 | cut -d "." -f 1)
echo $MEM

DISK_SIZE_GB=40

mkdir -p iso
if [ ! -f $UBUNTU_ISO ]; then
  wget $UBUNTU_URL -O $UBUNTU_ISO
fi

IMG_NAME=$VM_DIR/agamotto.qcow2
if [ ! -f $IMG_NAME ]; then
  qemu-img create -f qcow2 $IMG_NAME $DISK_SIZE_GB"G"
fi

exec qemu-system-x86_64 \
          -kernel $KERNEL \
          -initrd $INITRD \
          -nographic \
          -enable-kvm \
          -boot d \
          -cdrom $UBUNTU_ISO \
          -hda $IMG_NAME \
          -m $MEM \
          --append "console=ttyS0,115200n8" \
          -balloon virtio \
          -smp $(nproc) \
          -cpu host \
          -no-reboot

          #-netdev user,id=mynet \
          #-device virtio-net-pci,netdev=mynet \
          #-nographic -no-reboot \
