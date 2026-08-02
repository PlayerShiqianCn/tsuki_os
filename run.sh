#!/bin/bash
qemu-system-i386 \
  -m 256M \
  -vga std \
  -display vnc=127.0.0.1:0 \
  -serial file:build/qemu-vnc.log \
  -nic user,model=e1000 \
  -drive format=raw,file=build/os-image.img
