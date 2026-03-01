# Makefile

CFLAGS = -m32 -ffreestanding -fno-pie -Wall -Wextra -nostdlib -nodefaultlibs -nostartfiles -g -Iinclude
KERNEL_LDFLAGS = -m elf_i386 -T boot/link.ld
BUILD_DIR = build
MP_HEADER = include/mp.h
PRODUCT_VERSION = 0.3.0-project_teamo-alpha
VERSION_COUNTER = $(BUILD_DIR)/.build_version
FS_ROOT = $(BUILD_DIR)/fsroot
TSK_SLOT_BASE = $(shell awk '/MP_APP_SLOT_BASE/ { gsub(/u/, "", $$3); print $$3; exit }' $(MP_HEADER))
TSK_SLOT_SIZE = $(shell awk '/MP_APP_SLOT_SIZE/ { gsub(/u/, "", $$3); print $$3; exit }' $(MP_HEADER))

BOOT_BIN = $(BUILD_DIR)/boot.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
MKFS = $(BUILD_DIR)/mkfs
MAKE_TSK = $(BUILD_DIR)/make_tsk
LIB_TSO = $(BUILD_DIR)/lib.tso
JPEG_TSO = $(BUILD_DIR)/jpeg.tso
HELLO_TXT = $(BUILD_DIR)/hello.txt
VERSION_FILE = $(BUILD_DIR)/version.txt
START_REG = $(BUILD_DIR)/start.rtsk
CONFIG_REG = $(BUILD_DIR)/config.rtsk
TSK_GIRL_SOF0 = $(BUILD_DIR)/tsk_girl_sof0.jpg
OS_IMAGE = $(BUILD_DIR)/os-image.img
QEMU_LOG = $(BUILD_DIR)/qemu.log

define tsk_slot_addr
$(shell printf '0x%X' $$(( $(TSK_SLOT_BASE) + ($(1) * $(TSK_SLOT_SIZE)) )))
endef

APP_TSK_ADDR = $(call tsk_slot_addr,0)
TERMINAL_TSK_ADDR = $(call tsk_slot_addr,1)
WM_TSK_ADDR = $(call tsk_slot_addr,2)
START_TSK_ADDR = $(call tsk_slot_addr,3)
IMAGE_TSK_ADDR = $(call tsk_slot_addr,4)
SETTINGS_TSK_ADDR = $(call tsk_slot_addr,5)

KERNEL_C_SRCS = \
	kernel/idt.c \
	kernel/kernel.c \
	kernel/config.c \
	kernel/desktop.c \
	drivers/ps2.c \
	drivers/video.c \
	drivers/window.c \
	kernel/utils.c \
	kernel/heap.c \
	kernel/console.c \
	drivers/disk.c \
	fs/fs.c \
	kernel/timer.c \
	kernel/syscall.c \
	kernel/process.c \
	kernel/klog.c \
	drivers/pci.c \
	drivers/net.c
KERNEL_OBJS = \
	$(BUILD_DIR)/boot/kernel_entry.o \
	$(patsubst %.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRCS))

APP_TSK_ELF = $(BUILD_DIR)/app.tsk.elf
APP_TSK = $(BUILD_DIR)/app.tsk
TERMINAL_TSK_ELF = $(BUILD_DIR)/terminal.tsk.elf
TERMINAL_TSK = $(BUILD_DIR)/terminal.tsk
WM_TSK_ELF = $(BUILD_DIR)/wm.tsk.elf
WM_TSK = $(BUILD_DIR)/wm.tsk
START_TSK_ELF = $(BUILD_DIR)/start.tsk.elf
START_TSK = $(BUILD_DIR)/start.tsk
IMAGE_TSK_ELF = $(BUILD_DIR)/image.tsk.elf
IMAGE_TSK = $(BUILD_DIR)/image.tsk
SETTINGS_TSK_ELF = $(BUILD_DIR)/settings.tsk.elf
SETTINGS_TSK = $(BUILD_DIR)/settings.tsk
TSK_APPS = $(APP_TSK) $(TERMINAL_TSK) $(WM_TSK) $(START_TSK) $(IMAGE_TSK) $(SETTINGS_TSK)

.PHONY: all run clean FORCE

all: $(OS_IMAGE)

FORCE:

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot/%.o: boot/%.asm
	@mkdir -p $(dir $@)
	nasm -f elf $< -o $@

$(BOOT_BIN): boot/boot.asm
	@mkdir -p $(dir $@)
	nasm -f bin $< -o $@

$(KERNEL_BIN): $(KERNEL_OBJS) boot/link.ld
	@mkdir -p $(dir $@)
	ld $(KERNEL_LDFLAGS) $(KERNEL_OBJS) -o $(KERNEL_ELF)
	ld $(KERNEL_LDFLAGS) $(KERNEL_OBJS) -o $(KERNEL_BIN) --oformat binary

$(MKFS): tools/mkfs.c
	@mkdir -p $(dir $@)
	gcc tools/mkfs.c -o $@

$(MAKE_TSK): tools/make_tsk.c
	@mkdir -p $(dir $@)
	gcc tools/make_tsk.c -o $@

$(LIB_TSO): $(BUILD_DIR)/userspace/lib.o
	@mkdir -p $(dir $@)
	cp -f $< $@

$(JPEG_TSO): $(BUILD_DIR)/userspace/jpeg.o
	@mkdir -p $(dir $@)
	cp -f $< $@

$(APP_TSK_ELF): $(BUILD_DIR)/apps/app.o $(BUILD_DIR)/userspace/lib.o $(BUILD_DIR)/userspace/ui.o
	@mkdir -p $(dir $@)
	ld -m elf_i386 -N -e _start -Ttext $(APP_TSK_ADDR) -o $@ $^

$(APP_TSK): $(APP_TSK_ELF) $(MAKE_TSK)
	$(MAKE_TSK) $< $@

$(TERMINAL_TSK_ELF): $(BUILD_DIR)/apps/terminal_tsk.o $(BUILD_DIR)/userspace/lib.o $(BUILD_DIR)/userspace/ui.o
	@mkdir -p $(dir $@)
	ld -m elf_i386 -N -e _start -Ttext $(TERMINAL_TSK_ADDR) -o $@ $^

$(TERMINAL_TSK): $(TERMINAL_TSK_ELF) $(MAKE_TSK)
	$(MAKE_TSK) $< $@

$(WM_TSK_ELF): $(BUILD_DIR)/apps/wm_tsk.o $(BUILD_DIR)/userspace/lib.o $(BUILD_DIR)/userspace/ui.o
	@mkdir -p $(dir $@)
	ld -m elf_i386 -N -e _start -Ttext $(WM_TSK_ADDR) -o $@ $^

$(WM_TSK): $(WM_TSK_ELF) $(MAKE_TSK)
	$(MAKE_TSK) $< $@

$(START_TSK_ELF): $(BUILD_DIR)/apps/start_tsk.o $(BUILD_DIR)/userspace/lib.o $(BUILD_DIR)/userspace/ui.o
	@mkdir -p $(dir $@)
	ld -m elf_i386 -N -e _start -Ttext $(START_TSK_ADDR) -o $@ $^

$(START_TSK): $(START_TSK_ELF) $(MAKE_TSK)
	$(MAKE_TSK) $< $@

$(IMAGE_TSK_ELF): $(BUILD_DIR)/boot/image_entry.o $(BUILD_DIR)/apps/image_tsk.o $(BUILD_DIR)/userspace/jpeg.o $(BUILD_DIR)/userspace/lib.o $(BUILD_DIR)/userspace/ui.o
	@mkdir -p $(dir $@)
	ld -m elf_i386 -N -e _start -Ttext $(IMAGE_TSK_ADDR) -o $@ $^

$(IMAGE_TSK): $(IMAGE_TSK_ELF) $(MAKE_TSK)
	$(MAKE_TSK) $< $@

$(SETTINGS_TSK_ELF): $(BUILD_DIR)/boot/settings_entry.o $(BUILD_DIR)/apps/settings_tsk.o $(BUILD_DIR)/userspace/lib.o $(BUILD_DIR)/userspace/ui.o
	@mkdir -p $(dir $@)
	ld -m elf_i386 -N -e _start -Ttext $(SETTINGS_TSK_ADDR) -o $@ $^

$(SETTINGS_TSK): $(SETTINGS_TSK_ELF) $(MAKE_TSK)
	$(MAKE_TSK) $< $@

$(TSK_GIRL_SOF0): tsk_girl.jpg
	@mkdir -p $(dir $@)
	python3 -c "from PIL import Image; img = Image.open('tsk_girl.jpg').convert('RGB'); img.save('$(TSK_GIRL_SOF0)', format='JPEG', progressive=False, quality=85)"

$(VERSION_FILE): FORCE
	@mkdir -p $(dir $@)
	@v=0; \
	if [ -f $(VERSION_COUNTER) ]; then v=$$(cat $(VERSION_COUNTER)); fi; \
	case "$$v" in ''|*[!0-9]*) v=0 ;; esac; \
	v=$$((v + 1)); \
	echo $$v > $(VERSION_COUNTER); \
	printf "version=%s\nbuild=%s\n" "$(PRODUCT_VERSION)" "$$v" > $@

$(START_REG): FORCE
	@mkdir -p $(dir $@)
	@printf "# title|file|color\n" > $@
	@printf "Settings|settings.tsk|9\n" >> $@

$(CONFIG_REG): FORCE
	@mkdir -p $(dir $@)
	@printf "# key=value\n" > $@
	@printf "wallpaper=default\n" >> $@
	@printf "start_page=enabled\n" >> $@
	@printf "screen_w=640\n" >> $@
	@printf "screen_h=480\n" >> $@
	@printf "local_ip=10.0.2.15\n" >> $@
	@printf "gateway=10.0.2.2\n" >> $@
	@printf "dns=10.0.2.3\n" >> $@

$(OS_IMAGE): $(BOOT_BIN) $(KERNEL_BIN) $(MKFS) $(TSK_APPS) $(LIB_TSO) $(JPEG_TSO) $(VERSION_FILE) $(START_REG) $(CONFIG_REG) tsk_girl.jpg $(TSK_GIRL_SOF0)
	@mkdir -p $(dir $@)
	echo "Hello MultiTasking!" > $(HELLO_TXT)
	rm -rf $(FS_ROOT)
	mkdir -p $(FS_ROOT)/system $(FS_ROOT)/image
	cp -f $(VERSION_FILE) $(FS_ROOT)/system/version.txt
	cp -f $(START_REG) $(FS_ROOT)/system/start.rtsk
	cp -f $(CONFIG_REG) $(FS_ROOT)/system/config.rtsk
	cp -f $(HELLO_TXT) $(FS_ROOT)/system/hello.txt._hid_
	cp -f $(WM_TSK) $(FS_ROOT)/system/wm.tsk._hid_
	cp -f $(START_TSK) $(FS_ROOT)/system/start.tsk._hid_
	cp -f $(LIB_TSO) $(FS_ROOT)/system/lib.tso
	cp -f $(JPEG_TSO) $(FS_ROOT)/system/jpeg.tso
	cp -f tsk_girl.jpg $(FS_ROOT)/image/tsk_girl.jpg
	cp -f $(TSK_GIRL_SOF0) $(FS_ROOT)/image/tsk_girl_sof0.jpg
	$(MKFS) $(OS_IMAGE) $(BOOT_BIN) $(KERNEL_BIN) $(APP_TSK) $(TERMINAL_TSK) $(IMAGE_TSK) $(SETTINGS_TSK) $(FS_ROOT)/system/version.txt $(FS_ROOT)/system/start.rtsk $(FS_ROOT)/system/config.rtsk $(FS_ROOT)/system/hello.txt._hid_ $(FS_ROOT)/system/wm.tsk._hid_ $(FS_ROOT)/system/start.tsk._hid_ $(FS_ROOT)/system/lib.tso $(FS_ROOT)/system/jpeg.tso $(FS_ROOT)/image/tsk_girl.jpg $(FS_ROOT)/image/tsk_girl_sof0.jpg
	truncate -s 10M $(OS_IMAGE)

run: $(OS_IMAGE)
	qemu-system-i386 -vga std -d int -D $(QEMU_LOG) -nic user,model=e1000 -drive format=raw,file=$(OS_IMAGE)

clean:
	rm -f *.o *.bin *.img *.elf *.tsk *.tsk.elf *.tsk._hid_ *.txt._hid_ *.tso version.txt start.rtsk config.rtsk hello.txt .build_version tsk_girl.png tsk_girl.jrgb._hid_ tsk_girl.jr32._hid_ tsk_girl_sof0.jpg baseline_test.jpg baseline_test.rgb qemu.log qemu_vbe_test.log
	rm -f boot/*.o kernel/*.o drivers/*.o fs/*.o apps/*.o userspace/*.o
	rm -rf $(BUILD_DIR) .fsroot
