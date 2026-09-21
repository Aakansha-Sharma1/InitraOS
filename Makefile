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
CC         ?= gcc
LD         ?= ld
OBJCOPY    ?= objcopy
PYTHON     ?= python3
QEMU       ?= qemu-system-x86_64
BUILD      := build
NASMFLAGS  ?=

.PHONY: all run test clean

all: $(BUILD)/disk.img

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/stage2.bin: stage2.asm gdt.inc a20.inc longmode.inc paging64.inc | $(BUILD)
	$(NASM) -f bin $(NASMFLAGS) stage2.asm -o $@

# Compile the C kernel source as freestanding 32-bit code.
$(BUILD)/kernel.c.o: kernel.c | $(BUILD)
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector \
		-fno-asynchronous-unwind-tables -fno-unwind-tables \
		-c kernel.c -o $@

# Compile the first 64-bit C kernel entry as freestanding x86-64 code.
$(BUILD)/kernel64.c.o: kernel64.c | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pie -fno-stack-protector \
		-fno-asynchronous-unwind-tables -fno-unwind-tables \
		-mno-red-zone -O0 \
		-c kernel64.c -o $@

# Extract the self-contained 64-bit C text for embedding into the existing ELF32 kernel object.
$(BUILD)/kernel64.c.bin: $(BUILD)/kernel64.c.o
	$(OBJCOPY) -O binary --only-section=.text $< $@

# Assemble the low-level kernel entry/ISR code as ELF32.
$(BUILD)/kernel.asm.o: kernel.asm idt.inc serial.inc $(BUILD)/kernel64.c.bin | $(BUILD)
	$(NASM) -f elf32 $(NASMFLAGS) kernel.asm -o $@

# Link the C and assembly objects into an ELF kernel.
$(BUILD)/kernel.elf: $(BUILD)/kernel.asm.o $(BUILD)/kernel.c.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -o $@ \
		$(BUILD)/kernel.asm.o $(BUILD)/kernel.c.o

# Convert the linked ELF kernel into the flat binary loaded by stage2.
$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# boot.bin must be assembled AFTER stage2 and kernel,
# because the number of sectors it loads is derived from their actual sizes.
$(BUILD)/boot.bin: boot.asm $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	$(NASM) -f bin $(NASMFLAGS) boot.asm -o $@ \
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
	rm -rf build