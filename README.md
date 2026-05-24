# Vesper-OS

Vesper-OS is a serious x86 operating-systems project: a 32-bit educational kernel that boots from a custom sector loader and brings up interrupts, memory management, multitasking, filesystems, user-mode execution, and networking.

It is written from scratch in C + NASM and runs in QEMU.

---

## Why this project is more than “minimal”

Vesper-OS currently includes:

- **Custom boot path**: BIOS boot sector + protected-mode transition
- **Kernel core**: GDT, TSS, IDT, PIC, ISR/IRQ dispatch
- **Drivers**: PS/2 keyboard, PS/2 mouse, PIT timer, RTC, ATA (PIO), PCI, RTL8139 NIC, serial console
- **Memory management**: heap allocator, physical memory manager, paging with per-process page directories
- **Process model**: preemptive round-robin scheduler + cooperative yield
- **Privilege separation**: ring-3 user process support + `INT 0x80` syscall gate
- **IPC and handles**: pipe subsystem + file descriptor table
- **Storage and FS**: on-disk VesperFS with format/list/read/write/delete
- **Networking**: Ethernet + ARP + IPv4 + UDP + DHCP client
- **Interactive shell**: process, file, memory, timing, and network commands

---

## Boot and initialization flow

```text
BIOS
  -> bootloader/boot.asm (16-bit real mode, disk read via INT 13h)
  -> protected-mode switch
  -> kernel entry at 0x1000
  -> subsystem bring-up:
       VGA, serial, heap, GDT/TSS, IDT/PIC, timer, keyboard, mouse,
       PMM, paging, syscall gate, pipes/FDs, scheduler, ATA, VesperFS,
       RTL8139 + network stack, interrupts, DHCP
  -> interactive shell
```

---

## Quick start

### Prerequisites

Install toolchain + emulator (Ubuntu/Debian):

```bash
sudo apt-get update
sudo apt-get install -y nasm gcc-multilib qemu-system-x86 make
```

### Build

```bash
make
```

Output image:

```text
build/vesper.img
```

### Run

```bash
make run
```

Or manually:

```bash
qemu-system-i386 -drive format=raw,file=build/vesper.img,index=0,media=disk -m 32M -serial stdio
```

---

## Shell capabilities

| Command | Description |
|---|---|
| `help` | Show available commands |
| `clear` | Clear the screen |
| `echo <text>` | Print text |
| `version` | Show version/build info |
| `meminfo` | Heap + physical memory stats |
| `uptime` | Show timer uptime |
| `date` | Show RTC date/time |
| `ps` | List processes |
| `kill <pid>` | Terminate process |
| `sleep <ms>` | Sleep for milliseconds |
| `colortest` | Render VGA color table |
| `mouse` | Show PS/2 mouse state |
| `netinfo` | Show MAC/IP/netmask/gateway/DNS |
| `arp` | Show ARP cache |
| `dhcp` | Re-run DHCP discovery |
| `mkfs` | Format VesperFS |
| `ls` | List VesperFS files |
| `cat <file>` | Print file contents |
| `write <file> <text>` | Write text to file |
| `rm <file>` | Delete file |
| `run <file>` | Load ELF32 and spawn kernel thread |
| `exec <file>` | Load ELF32 and spawn ring-3 user process |
| `halt` | Halt CPU |
| `reboot` | Reboot system |

The shell also supports **command history** via Up/Down arrows.

---

## Architecture map

```text
bootloader/
  boot.asm                 512-byte boot sector, protected-mode transition

kernel/
  kernel.c                 boot orchestration and subsystem bring-up
  isr.asm + isr.c          exception/IRQ stubs and dispatcher
  gdt.*, tss.*, idt.*, pic.*  CPU tables + interrupt controller
  timer.*, rtc.*           PIT scheduler tick + real-time clock
  kmem.*, pmm.*, paging.*  heap + physical frame allocator + virtual memory
  process.* + process.asm  scheduler and context switching
  syscall.*                INT 0x80 syscall interface (ring 3 -> ring 0)
  pipe.*, fd.*             IPC pipes and descriptor table
  ata.*, fs.*              block I/O and VesperFS
  pci.*, rtl8139.*, net.*  NIC discovery, driver, and network stack
  keyboard.*, mouse.*      PS/2 input devices
  shell.*                  interactive command layer
```

---

## Development notes

- Default build uses native `gcc -m32` and `ld` (overridable with `CC=` and `LD=`).
- Kernel is linked as a flat binary loaded at `0x1000`.
- Disk layout in `build/vesper.img`:
  - Sector 0: boot sector
  - Sectors 1–128: kernel
  - Sector 129 onward: VesperFS partition

---

## Roadmap ideas

- SMP and APIC support
- Better userland tooling and ELF loading ergonomics
- More filesystems and storage drivers
- TCP/IP expansion beyond UDP/DHCP
- In-kernel test harnesses and CI boot validation

---

## License

No license file is currently included in this repository.
