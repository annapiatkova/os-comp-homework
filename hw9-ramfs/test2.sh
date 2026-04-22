#!/bin/sh

set -x

# load module
sudo /sbin/insmod ramfs.ko

# mount filesystem
sudo mkdir -p /mnt/ramfs
sudo mount -t ramfs none /mnt/ramfs
ls -laid /mnt/ramfs

cd /mnt/ramfs

# create directory
sudo mkdir mydir
ls -la

# create subdirectory
cd mydir
sudo mkdir mysubdir
ls -lai

# rename subdirectory
sudo mv mysubdir myrenamedsubdir
ls -lai

# delete renamed subdirectory
sudo rmdir myrenamedsubdir
ls -la

# create file
sudo touch myfile
ls -lai

# rename file
sudo mv myfile myrenamedfile
ls -lai

# delete renamed file
sudo rm myrenamedfile

# delete directory
cd ..
sudo rmdir mydir
ls -la

# unmount filesystem
cd ..
sudo umount /mnt/ramfs

# unload module
sudo /sbin/rmmod ramfs