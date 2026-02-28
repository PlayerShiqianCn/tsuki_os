# Makefile

CFLAGS = -m32 -ffreestanding -fno-pie -Wall -Wextra -nostdlib -nodefaultlibs -nostartfiles -g
LDFLAGS = -m elf_i386 -T link.ld --oformat binary
VERSION_COUNTER = .build_version
FS_ROOT = .fsroot

OBJS = kernel_entry.o idt.o kernel.o ps2.o video.o window.o utils.o heap.o console.o disk.o fs.o timer.o syscall.o process.o klog.o pci.o net.o
TSK_APPS = app.tsk terminal.tsk wm.tsk start.tsk image.tsk settings.tsk

.PHONY: all run clean FORCE

all: os-image.img

FORCE:

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

lib.tso: lib.o
	cp -f lib.o lib.tso

jpeg.o: jpeg.c
	gcc $(CFLAGS) -c jpeg.c -o jpeg.o

jpeg.tso: jpeg.o
	cp -f jpeg.o jpeg.tso

ui.o: ui.c
	gcc $(CFLAGS) -c ui.c -o ui.o

app.o: app.c
	gcc $(CFLAGS) -c app.c -o app.o

app.tsk: app.o lib.o ui.o
	ld -m elf_i386 -Ttext 0x300000 --oformat binary -o app.tsk app.o lib.o ui.o

terminal_tsk.o: terminal_tsk.c
	gcc $(CFLAGS) -c terminal_tsk.c -o terminal_tsk.o

terminal.tsk: terminal_tsk.o lib.o ui.o
	ld -m elf_i386 -Ttext 0x320000 --oformat binary -o terminal.tsk terminal_tsk.o lib.o ui.o

wm_tsk.o: wm_tsk.c
	gcc $(CFLAGS) -c wm_tsk.c -o wm_tsk.o

wm.tsk: wm_tsk.o lib.o ui.o
	ld -m elf_i386 -Ttext 0x340000 --oformat binary -o wm.tsk wm_tsk.o lib.o ui.o

start_tsk.o: start_tsk.c
	gcc $(CFLAGS) -c start_tsk.c -o start_tsk.o

start.tsk: start_tsk.o lib.o ui.o
	ld -m elf_i386 -Ttext 0x360000 --oformat binary -o start.tsk start_tsk.o lib.o ui.o

image_tsk.o: image_tsk.c
	gcc $(CFLAGS) -c image_tsk.c -o image_tsk.o

image_entry.o: image_entry.asm
	nasm -f elf image_entry.asm -o image_entry.o

image.tsk: image_entry.o image_tsk.o jpeg.o lib.o ui.o
	ld -m elf_i386 -Ttext 0x380000 --oformat binary -o image.tsk image_entry.o image_tsk.o jpeg.o lib.o ui.o

settings_tsk.o: settings_tsk.c
	gcc $(CFLAGS) -c settings_tsk.c -o settings_tsk.o

settings_entry.o: settings_entry.asm
	nasm -f elf settings_entry.asm -o settings_entry.o

settings.tsk: settings_entry.o settings_tsk.o lib.o ui.o
	ld -m elf_i386 -Ttext 0x3A0000 --oformat binary -o settings.tsk settings_entry.o settings_tsk.o lib.o ui.o

tsk_girl_sof0.jpg: tsk_girl.jpg
	python3 -c "from PIL import Image; img = Image.open('tsk_girl.jpg').convert('RGB'); img.save('tsk_girl_sof0.jpg', format='JPEG', progressive=False, quality=85)"

tsk_girl.jr32._hid_: tsk_girl.jpg mkpng.py
	python3 mkpng.py

version.txt: FORCE
	@v=0; \
	if [ -f $(VERSION_COUNTER) ]; then v=$$(cat $(VERSION_COUNTER)); fi; \
	case "$$v" in ''|*[!0-9]*) v=0 ;; esac; \
	v=$$((v + 1)); \
	echo $$v > $(VERSION_COUNTER); \
	echo $$v > version.txt

start.rtsk: FORCE
	@printf "# title|file|color\n" > start.rtsk
	@printf "Settings|settings.tsk|9\n" >> start.rtsk

config.rtsk: FORCE
	@printf "# key=value\n" > config.rtsk
	@printf "wallpaper=default\n" >> config.rtsk
	@printf "start_page=enabled\n" >> config.rtsk
	@printf "screen_w=640\n" >> config.rtsk
	@printf "screen_h=480\n" >> config.rtsk
	@printf "local_ip=10.0.2.15\n" >> config.rtsk
	@printf "gateway=10.0.2.2\n" >> config.rtsk
	@printf "dns=10.0.2.3\n" >> config.rtsk

os-image.img: boot.bin kernel.bin mkfs $(TSK_APPS) lib.tso jpeg.tso version.txt start.rtsk config.rtsk tsk_girl.jpg tsk_girl_sof0.jpg tsk_girl.jr32._hid_
	echo "Hello MultiTasking!" > hello.txt
	rm -rf $(FS_ROOT)
	mkdir -p $(FS_ROOT)/system $(FS_ROOT)/image
	cp -f version.txt $(FS_ROOT)/system/version.txt
	cp -f start.rtsk $(FS_ROOT)/system/start.rtsk
	cp -f config.rtsk $(FS_ROOT)/system/config.rtsk
	cp -f hello.txt $(FS_ROOT)/system/hello.txt._hid_
	cp -f wm.tsk $(FS_ROOT)/system/wm.tsk._hid_
	cp -f start.tsk $(FS_ROOT)/system/start.tsk._hid_
	cp -f lib.tso $(FS_ROOT)/system/lib.tso
	cp -f jpeg.tso $(FS_ROOT)/system/jpeg.tso
	cp -f tsk_girl.jpg $(FS_ROOT)/image/tsk_girl.jpg
	cp -f tsk_girl_sof0.jpg $(FS_ROOT)/image/tsk_girl_sof0.jpg
	cp -f tsk_girl.jr32._hid_ $(FS_ROOT)/image/tsk_girl.jr32._hid_
	./mkfs os-image.img boot.bin kernel.bin app.tsk terminal.tsk image.tsk settings.tsk $(FS_ROOT)/system/version.txt $(FS_ROOT)/system/start.rtsk $(FS_ROOT)/system/config.rtsk $(FS_ROOT)/system/hello.txt._hid_ $(FS_ROOT)/system/wm.tsk._hid_ $(FS_ROOT)/system/start.tsk._hid_ $(FS_ROOT)/system/lib.tso $(FS_ROOT)/system/jpeg.tso $(FS_ROOT)/image/tsk_girl.jpg $(FS_ROOT)/image/tsk_girl_sof0.jpg $(FS_ROOT)/image/tsk_girl.jr32._hid_
	truncate -s 10M os-image.img

run: os-image.img
	qemu-system-i386 -vga std -d int -D qemu.log -nic user,model=e1000 -drive format=raw,file=os-image.img

clean:
	rm -f *.o *.bin *.img *.elf mkfs *.tsk *.tsk._hid_ *.txt._hid_ *.tso version.txt start.rtsk config.rtsk tsk_girl.png tsk_girl.jrgb._hid_ tsk_girl.jr32._hid_ tsk_girl_sof0.jpg baseline_test.jpg baseline_test.rgb
	rm -rf $(FS_ROOT)
