#!/bin/sh

set -ex

#load module
sudo /sbin/insmod ramfs.ko

#mount filesystem
sudo mkdir -p /mnt/ramfs
sudo mount -t ramfs none /mnt/ramfs

#show registered filesystems
cat /proc/filesystems | grep ramfs

#show mounted filesystems
cat /proc/mounts | grep ramfs

#show filesystem statistics
stat -f /mnt/ramfs

#list all filesystem files
cd /mnt/ramfs
ls -la

#unmount filesystem
cd ..
sudo umount /mnt/ramfs

#unload module
sudo /sbin/rmmod ramfs