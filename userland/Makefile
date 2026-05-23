# =============================================================================
# Vesper OS – Userland Build System
# =============================================================================
#
# Builds user-space ELF32 binaries that can be loaded by the kernel via
# the 'exec' shell command.
#
# Usage:
#   make              Build all user programs
#   make clean        Remove build artefacts
# =============================================================================

CC  ?= gcc
LD  ?= ld
AS  := nasm

# Same flags as the kernel for 32-bit freestanding code
CFLAGS  := -m32 -ffreestanding -fno-pie -fno-stack-protector \
           -nostdlib -Wall -Wextra -O2 -I libc

ASFLAGS := -f elf32
LDFLAGS := -m elf_i386 -T link.ld

# Directories
BUILD_DIR  := build
LIBC_DIR   := libc
PROG_DIR   := programs

# Library sources
LIBC_OBJS  := $(BUILD_DIR)/start.o $(BUILD_DIR)/vesper.o

# Discover all program sources
PROG_SRCS  := $(wildcard $(PROG_DIR)/*.c)
PROG_ELFS  := $(patsubst $(PROG_DIR)/%.c, $(BUILD_DIR)/%.elf, $(PROG_SRCS))

# =============================================================================
.PHONY: all clean

all: $(PROG_ELFS)

# Link each program with the libc objects
$(BUILD_DIR)/%.elf: $(BUILD_DIR)/%.o $(LIBC_OBJS) link.ld | $(BUILD_DIR)
	@echo "[LD]   $@"
	$(LD) $(LDFLAGS) $(BUILD_DIR)/start.o $(BUILD_DIR)/vesper.o $< -o $@

# Compile program .c → .o
$(BUILD_DIR)/%.o: $(PROG_DIR)/%.c | $(BUILD_DIR)
	@echo "[CC]   $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Compile libc vesper.c → vesper.o
$(BUILD_DIR)/vesper.o: $(LIBC_DIR)/vesper.c $(LIBC_DIR)/vesper.h | $(BUILD_DIR)
	@echo "[CC]   $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble the CRT0 startup
$(BUILD_DIR)/start.o: $(LIBC_DIR)/start.asm | $(BUILD_DIR)
	@echo "[ASM]  $<"
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
