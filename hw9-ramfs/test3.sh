#!/bin/sh

set -ex

# load module
sudo /sbin/insmod ramfs.ko

# mount filesystem
sudo mkdir -p /mnt/ramfs
sudo mount -t ramfs none /mnt/ramfs
ls -laid /mnt/ramfs

cd /mnt/ramfs

# create file
sudo touch myfile
ls -lai

# rename file
sudo mv myfile myrenamedfile
ls -lai

# create link to file
sudo ln myrenamedfile mylink
ls -lai

# read/write file
echo message | sudo tee myrenamedfile
cat myrenamedfile

# remove link to file
sudo rm mylink
ls -la

# delete file
sudo rm -f myrenamedfile
ls -la

# unmount filesystem
cd ..
sudo umount /mnt/ramfs

# unload module
sudo /sbin/rmmod ramfs