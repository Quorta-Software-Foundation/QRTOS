#!/bin/bash
#!/bin/bash
echo "Building QRTOS (x86_64 long mode with 2-stage boot)..."

nasm -f bin boot/boot.asm -o boot/boot.bin
echo "[OK] Stage 1 bootloader assembled (512 bytes)"

nasm -f elf64 kernel/stub32.asm -o kernel/stub32.o
echo "[OK] Stage 2 stub32 assembled (32-bit -> 64-bit transition)"

gcc -m64 -ffreestanding -fno-pie -nostdlib -nostdinc \
    -fno-builtin -fno-stack-protector -Os -I. \
    -c kernel/kernel64.c -o kernel/kernel.o
echo "[OK] Kernel compiled (x86_64 64-bit)"

gcc -m64 -ffreestanding -fno-pie -nostdlib -nostdinc \
    -fno-builtin -fno-stack-protector -Os -I. \
    -c graphics/graphics.c -o graphics/graphics.o
echo "[OK] Graphics compiled (64-bit)"

gcc -m64 -ffreestanding -fno-pie -nostdlib -nostdinc \
    -fno-builtin -fno-stack-protector -Os -I. \
    -c fs/fas32q.c -o fs/fas32q.o
echo "[OK] FAS32Q filesystem compiled (64-bit)"

ld -m elf_x86_64 -T kernel/linker.ld \
   -o kernel/kernel.bin \
   --oformat binary \
   kernel/stub32.o kernel/kernel.o fs/fas32q.o graphics/graphics.o
echo "[OK] Kernel image linked"

echo "Kernel image size:"
ls -lh kernel/kernel.bin

dd if=/dev/zero of=qrtos.img bs=512 count=2880 2>/dev/null
dd if=boot/boot.bin of=qrtos.img conv=notrunc bs=512 seek=0 2>/dev/null
dd if=kernel/kernel.bin of=qrtos.img conv=notrunc bs=512 seek=1 2>/dev/null
echo "[OK] Bootable disk image created (qrtos.img)"

echo ""
echo "Build complete!"
echo "Run with: qemu-system-x86_64 -drive format=raw,file=qrtos.img,if=floppy -boot a"

