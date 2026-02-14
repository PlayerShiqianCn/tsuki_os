# Makefile

CFLAGS = -m32 -ffreestanding -fno-pie -Wall -Wextra -nostdlib -nodefaultlibs -nostartfiles -g
LDFLAGS = -m elf_i386 -T link.ld --oformat binary

# 【必须】加入 process.o
OBJS = kernel_entry.o idt.o kernel.o ps2.o video.o window.o utils.o heap.o console.o disk.o fs.o timer.o syscall.o process.o

all: os-image.img

boot.bin: boot.asm
	nasm -f bin boot.asm -o boot.bin

kernel_entry.o: kernel_entry.asm
	nasm -f elf kernel_entry.asm -o kernel_entry.o 

%.o: %.c
	gcc $(CFLAGS) -c $< -o $@

kernel.bin: $(OBJS) link.ld
	ld -m elf_i386 -T link.ld $(OBJS) -o kernel.elf
	ld -m elf_i386 -T link.ld $(OBJS) -o kernel.bin --oformat binary

mkfs: mkfs.c
	gcc mkfs.c -o mkfs



lib.o: lib.c
	gcc $(CFLAGS) -c lib.c -o lib.o

app.o: app.c
	gcc $(CFLAGS) -c app.c -o app.o

# 【重点修改】直接生成纯二进制 app.tsk，不需要 app.elf 和 make_tsk 了
app.tsk: app.o lib.o
	ld -m elf_i386 -Ttext 0x300000 --oformat binary -o app.tsk app.o lib.o

os-image.img: boot.bin kernel.bin mkfs app.tsk
	echo "Hello MultiTasking!" > hello.txt
	./mkfs os-image.img boot.bin kernel.bin hello.txt app.tsk
	truncate -s 10M os-image.img

run: os-image.img
	qemu-system-i386 -d int -D qemu.log -drive format=raw,file=os-image.img

clean:
	rm -f *.o *.bin *.img *.elf mkfs app.tsk