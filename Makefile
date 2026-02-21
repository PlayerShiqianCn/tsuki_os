# Makefile

CFLAGS = -m32 -ffreestanding -fno-pie -Wall -Wextra -nostdlib -nodefaultlibs -nostartfiles -g
LDFLAGS = -m elf_i386 -T link.ld --oformat binary

OBJS = kernel_entry.o idt.o kernel.o ps2.o video.o window.o utils.o heap.o console.o disk.o fs.o timer.o syscall.o process.o
TSK_APPS = app.tsk terminal.tsk wm.tsk start.tsk

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

app.tsk: app.o lib.o
	ld -m elf_i386 -Ttext 0x300000 --oformat binary -o app.tsk app.o lib.o

terminal_tsk.o: terminal_tsk.c
	gcc $(CFLAGS) -c terminal_tsk.c -o terminal_tsk.o

terminal.tsk: terminal_tsk.o lib.o
	ld -m elf_i386 -Ttext 0x320000 --oformat binary -o terminal.tsk terminal_tsk.o lib.o

wm_tsk.o: wm_tsk.c
	gcc $(CFLAGS) -c wm_tsk.c -o wm_tsk.o

wm.tsk: wm_tsk.o lib.o
	ld -m elf_i386 -Ttext 0x340000 --oformat binary -o wm.tsk wm_tsk.o lib.o

start_tsk.o: start_tsk.c
	gcc $(CFLAGS) -c start_tsk.c -o start_tsk.o

start.tsk: start_tsk.o lib.o
	ld -m elf_i386 -Ttext 0x360000 --oformat binary -o start.tsk start_tsk.o lib.o

os-image.img: boot.bin kernel.bin mkfs $(TSK_APPS)
	echo "Hello MultiTasking!" > hello.txt
	./mkfs os-image.img boot.bin kernel.bin hello.txt $(TSK_APPS)
	truncate -s 10M os-image.img

run: os-image.img
	qemu-system-i386 -d int -D qemu.log -drive format=raw,file=os-image.img

clean:
	rm -f *.o *.bin *.img *.elf mkfs *.tsk
