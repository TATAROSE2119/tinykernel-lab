#!/bin/bash
if [ $EUID -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

depmod
insmod  /lib/modules/4.1.15/tinyLinux_IoT_kernellab/leddriver.ko

if lsmod | grep -q leddriver; then
    echo "leddriver module is loaded"
else
    echo "leddriver module is not loaded"
    exit 1
fi

if [ ! -e /dev/led-dts-platform ]; then
    echo "/dev/led-dts-platform does not exist"
fi

./myctl