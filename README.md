# Vesper-OS

A minimal educational operating system built from scratch using Assembly, C, and QEMU.

## Boot Flow

```
BIOS  →  Bootloader (ASM, real mode)
      →  32-bit Protected Mode
      →  Kernel (C): heap → IDT → PIC → keyboard → STI
      →  Shell (interactive)
```

## Project Structure

```
Vesper-OS/
├── bootloader/
│   └── boot.asm          # 512-byte NASM boot sector
├── kernel/
│   ├── kernel_entry.asm  # Protected-mode ASM stub (entry at 0x1000)
│   ├── kernel.c / .h     # Kernel initialisation, boot banner
│   ├── vga.c / .h        # VGA text-mode driver (0xB8000), print_uint/hex
│   ├── port_io.h         # Shared inb / outb / io_wait helpers
│   ├── pic.c / .h        # 8259A PIC remap + IRQ mask/unmask/EOI
│   ├── idt.c / .h        # 256-entry IDT, idt_set_gate, lidt
│   ├── isr.asm           # ISR/IRQ stubs (exceptions 0-19, IRQs 0-15)
│   ├── isr.c / .h        # registers_t, C dispatcher, IRQ handler table
│   ├── keyboard.c / .h   # Interrupt-driven PS/2 keyboard (IRQ1, ring buffer)
│   ├── kmem.c / .h       # First-fit heap allocator (256 KB)
│   └── shell.c / .h      # Interactive command shell
├── linker.ld             # Kernel linker script (base = 0x1000)
├── Makefile              # Build system
└── README.md
```

## Memory Layout

| Address range | Contents                        |
|---------------|---------------------------------|
| 0x0000–0x04FF | BIOS data area / IVT            |
| 0x1000–0x4FFF | Kernel image (loaded here)      |
| 0x7C00–0x7DFF | Bootloader (this boot sector)   |
| 0xB8000       | VGA text-mode buffer            |
| 0x90000       | Kernel stack base (grows ↓)     |

## Prerequisites

| Tool              | Purpose                      |
|-------------------|------------------------------|
| `nasm`            | Assemble bootloader & stubs  |
| `gcc` (with `-m32`) or `i686-elf-gcc` | Compile the kernel |
| `ld` / `i686-elf-ld` | Link kernel to flat binary |
| `qemu-system-i386` | Emulate and test            |
| `make`            | Drive the build              |

Install on Ubuntu/Debian:

```bash
sudo apt-get install nasm gcc-multilib qemu-system-x86 make
```

## Building

```bash
make
```

The disk image is written to `build/vesper.img`.

## Running

```bash
make run
```

Or launch QEMU manually:

```bash
qemu-system-i386 -drive format=raw,file=build/vesper.img,index=0,media=disk -m 32M
```

## Boot Screenshot (v0.2.0)

![VESPER OS v0.2.0 boot](https://github.com/user-attachments/assets/757c50d5-66e2-49f1-852d-3b452fcfd4b9)

## Shell Commands

| Command        | Description                         |
|----------------|-------------------------------------|
| `help`         | List available commands             |
| `clear`        | Clear the screen                    |
| `echo <text>`  | Print text to the screen            |
| `version`      | Show OS version information         |
| `meminfo`      | Show heap memory statistics         |
| `halt`         | Halt the CPU                        |
| `reboot`       | Reboot the system                   |

## Architecture Notes

### Phase 1 – Bootloader (`bootloader/boot.asm`)
Runs in 16-bit real mode.  Uses BIOS INT 13h to load 128 sectors (64 KB)
of kernel binary from disk sector 1 into physical address 0x1000.  Detects
available RAM via E820 memory map.  Sets up a flat GDT (null / 4 GB code /
4 GB data), enables protected mode via `CR0.PE`, far-jumps to flush the
pipeline, sets up the kernel stack at 0x90000, and jumps to the kernel.

### Phase 2 – Kernel (`kernel/`)
* **`kernel_entry.asm`** — first 32-bit code at 0x1000; zeroes BSS, calls `kernel_main()`.
* **`vga.c`** — direct writes to the VGA text buffer at 0xB8000 (80×25, 16
  colours, scrolling, backspace, newlines, `vga_printf`).
* **`kernel.c`** — `kernel_main()` initialises every subsystem in order and
  then enters the interactive shell.

### Phase 3 – Shell (`kernel/shell.c`)
A read-eval-print loop with a green `vesper>` prompt, command history
(up/down arrows), and 20+ built-in commands including process management,
file I/O, and ELF binary execution.

### Phase 4 – Interrupt Handling
* **`port_io.h`** — shared `inb`/`outb`/`io_wait` inline helpers.
* **`pic.c`** — remaps the 8259A PIC so IRQ0–15 use INT 32–47 (no conflict
  with CPU exception vectors 0–19).  All lines masked initially.
* **`idt.c`** — fills all 256 IDT slots and loads the table with `LIDT`.
* **`isr.asm`** — NASM-macro-generated stubs for exceptions 0–19 and IRQs
  0–15; all converge on a single `common_stub` that saves/restores registers
  and calls the C dispatcher.
* **`isr.c`** — `interrupt_handler()` dispatches CPU exceptions (kills the
  faulting user process or halts if in kernel mode) and hardware IRQs (calls
  the registered driver handler, then sends PIC EOI).

### Phase 5 – Memory Management (`kernel/kmem.c`, `kernel/pmm.c`, `kernel/paging.c`)
A 256 KB static heap (`uint8_t heap_storage[262144]` in BSS) managed by a
first-fit linked-list allocator.  Each block carries a 12-byte header
(`size`, `free`, `magic`).  `kfree()` coalesces adjacent free blocks.
The PMM provides a bitmap-based physical frame allocator (up to 32 MB).
Paging identity-maps the first 8 MB; user processes get isolated page
directories.

### Interrupt-driven Keyboard (`kernel/keyboard.c`)
The PS/2 driver registers an IRQ1 handler that decodes PS/2 Scancode Set 1
make/break codes (with Shift tracking) and pushes ASCII characters into a 64-
byte volatile ring buffer.  `keyboard_getchar()` blocks via targeted wait-
reason tagging — only keyboard-waiting processes are woken on keypress.

### Disk & Filesystem
* **`ata.c`** — polling ATA PIO driver for primary master (28-bit LBA).
* **`fs.c`** — VesperFS: flat filesystem starting at LBA 129, supports
  create/read/delete with deleted-space reuse for writes.

### Process Management & Scheduling
* **`process.c`** / **`process.asm`** — preemptive round-robin scheduler
  (50 ms timeslice via PIT), per-process kernel stacks, targeted wakeups.
* **`syscall.c`** — INT 0x80 syscall gate (DPL=3) with user pointer
  validation to prevent kernel memory corruption from ring-3 code.
* **`elf.c`** — ELF32 loader for kernel threads and ring-3 user processes.

### Disk Image Layout
| Sector range | Contents                        |
|--------------|---------------------------------|
| 0            | Boot sector (512 B)             |
| 1–128        | Kernel binary (64 KB budget)    |
| 129–8191     | VesperFS partition (~4 MB)      |
