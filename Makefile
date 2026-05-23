# =============================================================================
# VESPER OS – Build System
# =============================================================================
#
# Usage:
#   make          Build the OS disk image  (build/vesper.img)
#   make run      Launch in QEMU
#   make clean    Remove all build artefacts
#
# Toolchain override for cross-compilation (default: native gcc -m32):
#   make CC=i686-elf-gcc LD=i686-elf-ld
# =============================================================================

CC  ?= gcc
LD  ?= ld
AS  := nasm

# -ffreestanding : no stdlib, no main() assumption
# -fno-pie        : no position-independent code (fixed load address)
# -fno-stack-protector : no __stack_chk_fail reference
# -nostdlib       : do not link against libc/crt
# -m32            : generate 32-bit code on a 64-bit host
CFLAGS  := -m32 -ffreestanding -fno-pie -fno-stack-protector \
           -nostdlib -Wall -Wextra -O2

ASFLAGS := -f elf32                                # NASM → 32-bit ELF object

LDFLAGS := -m elf_i386 \
           -T linker.ld \
           --oformat binary                        # Flat binary output

# -----------------------------------------------------------------------------
# Directory layout
# -----------------------------------------------------------------------------
BOOT_DIR   := bootloader
KERNEL_DIR := kernel
BUILD_DIR  := build

# -----------------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------------
BOOT_SRC       := $(BOOT_DIR)/boot.asm
KERNEL_ENTRY   := $(KERNEL_DIR)/kernel_entry.asm

# List C sources explicitly so the link order is deterministic
KERNEL_C_SRCS  := $(KERNEL_DIR)/kernel.c   \
                  $(KERNEL_DIR)/vga.c       \
                  $(KERNEL_DIR)/keyboard.c  \
                  $(KERNEL_DIR)/shell.c     \
                  $(KERNEL_DIR)/pic.c       \
                  $(KERNEL_DIR)/idt.c       \
                  $(KERNEL_DIR)/isr.c       \
                  $(KERNEL_DIR)/kmem.c      \
                  $(KERNEL_DIR)/serial.c    \
                  $(KERNEL_DIR)/timer.c     \
                  $(KERNEL_DIR)/pmm.c       \
                  $(KERNEL_DIR)/paging.c    \
                  $(KERNEL_DIR)/gdt.c       \
                  $(KERNEL_DIR)/tss.c       \
                  $(KERNEL_DIR)/process.c   \
                  $(KERNEL_DIR)/syscall.c   \
                  $(KERNEL_DIR)/ata.c       \
                  $(KERNEL_DIR)/fs.c        \
                  $(KERNEL_DIR)/elf.c       \
                  $(KERNEL_DIR)/rtc.c       \
                  $(KERNEL_DIR)/pipe.c      \
                  $(KERNEL_DIR)/fd.c

# Object files: kernel_entry.o must come FIRST so it lands at 0x1000
KERNEL_OBJS := $(BUILD_DIR)/kernel_entry.o  \
               $(BUILD_DIR)/isr_stubs.o      \
               $(BUILD_DIR)/process_ctx.o    \
               $(patsubst $(KERNEL_DIR)/%.c, $(BUILD_DIR)/%.o, $(KERNEL_C_SRCS))

# -----------------------------------------------------------------------------
# Output targets
# -----------------------------------------------------------------------------
BOOT_BIN  := $(BUILD_DIR)/boot.bin
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
OS_IMAGE  := $(BUILD_DIR)/vesper.img

# -----------------------------------------------------------------------------
# Phony targets
# -----------------------------------------------------------------------------
.PHONY: all run clean

all: $(OS_IMAGE)

# Launch in QEMU using the raw disk image
run: $(OS_IMAGE)
	qemu-system-i386 -drive format=raw,file=$(OS_IMAGE),index=0,media=disk -m 32M -serial stdio

# Same as run but with a QEMU monitor on stdio for interactive debugging
run-debug: $(OS_IMAGE)
	qemu-system-i386 -drive format=raw,file=$(OS_IMAGE),index=0,media=disk \
	                 -m 32M -serial file:/dev/null -monitor stdio

clean:
	rm -rf $(BUILD_DIR)

# -----------------------------------------------------------------------------
# Disk image assembly
#
#  1. Create a blank 4 MB disk image (8192 × 512-byte sectors).
#  2. Write the 512-byte boot sector to sector 0 (LBA 0).
#  3. Write the kernel binary starting at sector 1 (LBA 1),
#     which is where the bootloader loads from.
# -----------------------------------------------------------------------------
$(OS_IMAGE): $(BOOT_BIN) $(KERNEL_BIN) | $(BUILD_DIR)
	@echo "[IMG]  $@"
	# Disk layout:
	#   Sector   0        : boot sector (512 B)
	#   Sectors  1–128    : kernel binary (64 KB budget)
	#   Sectors  129–8191 : VesperFS partition
	dd if=/dev/zero        of=$@ bs=512 count=8192   2>/dev/null
	dd if=$(BOOT_BIN)      of=$@ bs=512 conv=notrunc 2>/dev/null
	dd if=$(KERNEL_BIN)    of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null

# Assemble the bootloader to a flat binary
$(BOOT_BIN): $(BOOT_SRC) | $(BUILD_DIR)
	@echo "[ASM]  $<"
	$(AS) -f bin $< -o $@

# Link all kernel objects into a flat binary at 0x1000
$(KERNEL_BIN): $(KERNEL_OBJS) linker.ld | $(BUILD_DIR)
	@echo "[LD]   $@"
	$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o $@

# Compile the kernel entry assembly stub
$(BUILD_DIR)/kernel_entry.o: $(KERNEL_ENTRY) | $(BUILD_DIR)
	@echo "[ASM]  $<"
	$(AS) $(ASFLAGS) $< -o $@

# Compile the ISR / IRQ stubs
$(BUILD_DIR)/isr_stubs.o: $(KERNEL_DIR)/isr.asm | $(BUILD_DIR)
	@echo "[ASM]  $<"
	$(AS) $(ASFLAGS) $< -o $@

# Compile the process context-switch stub
$(BUILD_DIR)/process_ctx.o: $(KERNEL_DIR)/process.asm | $(BUILD_DIR)
	@echo "[ASM]  $<"
	$(AS) $(ASFLAGS) $< -o $@

# Compile C kernel source files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	@echo "[CC]   $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Create the build directory if it does not exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
