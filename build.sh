#!/bin/bash
# CROSS_COMPILE=gcc-prefix- ./build.sh --board <board>
if [[ -z "$CROSS_COMPILE" ]]; then
	echo "CROSS_COMPILE not set!"
	exit 1
fi

BOARD="$2"
if [[ "$BOARD" == 0 || -z "$BOARD" ]]; then
	echo "usage: ./build.sh --board <name>"
	exit 1
fi

#make ARCH=arm CROSS_COMPILE=${CROSS_COMPILE} mt6771_defconfig "$BOARD".config
make ARCH=arm CROSS_COMPILE=${CROSS_COMPILE} LOCALVERSION="-$BOARD" -j$(nproc)
gzip -9nc u-boot.bin > /tmp/u-boot.bin.gz
dd if=/dev/random of=/tmp/ramdisk bs=2048 count=1
gzip -9nc /tmp/ramdisk > /tmp/ramdisk.gz
mkbootimg --header_version 2 --kernel /tmp/u-boot.bin.gz --ramdisk /tmp/ramdisk.gz \
	--dtb arch/arm/mach-mediatek/mt6771/pda-dtb \
	--cmdline "bootopt=64S3,32N2,64N2" \
	--pagesize 2048 --base 0x40078000 \
	--kernel_offset 0x00008000 --ramdisk_offset 0x14f88000 \
	--second_offset 0xbff88000 --tags_offset 0x13f88000 \
	--dtb_offset 0x13f88000 -o mt6771-boot-"$BOARD".img

# cleanup after we're done
rm /tmp/u-boot.bin.gz
rm /tmp/ramdisk
rm /tmp/ramdisk.gz
