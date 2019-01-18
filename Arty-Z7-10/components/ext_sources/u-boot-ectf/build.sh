cd ~/u-boot-ectf
ARCH=arm CROSS_COMPILE=/opt/pkg/petalinux/tools/linux-i386/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf- make zynq_ectf_defconfig && ARCH=arm CROSS_COMPILE=/opt/pkg/petalinux/tools/linux-i386/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf- make && cp ~/u-boot-ectf/u-boot ~/Desktop/uboot/u-boot.elf
cd ~/Desktop/uboot
/opt/pkg/petalinux/tools/hsm/bin/bootgen -arch zynq -image SystemImage.bif -w -o MES.bin
 
mount /dev/sdb1 /mnt/zynq/boot
cp ./BOOT.bin /mnt/zynq/boot
cp ./MES.bin /mnt/zynq/boot
