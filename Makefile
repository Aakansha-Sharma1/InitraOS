# InitraOS build system
#
#   make            build build/disk.img
#   make run        build and boot in QEMU (interactive)
#   make test       build an autoboot image and assert on serial output
#   make clean      remove build artifacts
#
# On native Windows use build.bat instead; this Makefile targets WSL,
# Linux and the CI runner.

NASM       ?= nasm
PYTHON     ?= python3
QEMU       ?= qemu-system-i386
BUILD      := build
NASMFLAGS  ?=

.PHONY: all run test clean

all: $(BUILD)/disk.img

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/stage2.bin: stage2.asm gdt.inc a20.inc | $(BUILD)
	$(NASM) -f bin $(NASMFLAGS) stage2.asm -o $@

$(BUILD)/kernel.bin: kernel.asm idt.inc serial.inc | $(BUILD)
	$(NASM) -f bin $(NASMFLAGS) kernel.asm -o $@

# boot.bin must be assembled AFTER stage2 and kernel, because the number of
# sectors it loads is derived from their actual sizes.
$(BUILD)/boot.bin: boot.asm $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	$(NASM) -f bin boot.asm -o $@ \
		-DLOAD_SECTORS=$$($(PYTHON) tools/mkimage.py --sectors-only)

$(BUILD)/disk.img: $(BUILD)/boot.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	$(PYTHON) tools/mkimage.py

run: all
	$(QEMU) -drive file=$(BUILD)/disk.img,format=raw,if=ide \
		-serial stdio -no-reboot

test:
	$(MAKE) clean
	$(MAKE) NASMFLAGS=-DAUTOBOOT all
	$(PYTHON) tools/boottest.py

clean:
	rm -rf $(BUILD)
