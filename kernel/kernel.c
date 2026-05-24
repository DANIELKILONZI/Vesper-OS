#include "kernel.h"
#include "vga.h"
#include "serial.h"
#include "shell.h"
#include "kmem.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "rtc.h"
#include "pmm.h"
#include "paging.h"
#include "gdt.h"
#include "tss.h"
#include "process.h"
#include "syscall.h"
#include "pipe.h"
#include "fd.h"
#include "ata.h"
#include "fs.h"
#include "rtl8139.h"
#include "net.h"
#include "port_io.h"

static void debug_puts(const char *s)
{
    while (*s) {
        outb(0xE9u, (uint8_t)*s++);
    }
}

/* -------------------------------------------------------------------------
 * kernel_main – called by kernel_entry.asm immediately after BSS is zeroed.
 *
 * Initialisation order (Tier 4):
 *   1.  vga_init()      – screen
 *   2.  serial_init()   – COM1
 *   3.  kmem_init()     – heap
 *   4.  tss_init()      – TSS
 *   5.  gdt_init()      – GDT
 *   6.  tss_flush()     – LTR
 *   7.  idt_init()      – IDT
 *   8.  pic_init()      – PIC remap
 *   9.  timer_init()    – PIT 100 Hz, preemptive slices
 *   10. keyboard_init() – PS/2 keyboard (IRQ1)
 *   11. mouse_init()    – PS/2 mouse (IRQ12)
 *   12. pmm_init()      – bitmap PMM
 *   13. paging_init()   – identity-map 0–8 MB, enable CR0.PG
 *   14. syscall_init()  – INT 0x80 (DPL=3, 13 syscalls)
 *   15. pipe_init()     – IPC pipe table
 *   16. fd_init()       – file descriptor table
 *   17. process_init()  – idle process, preemptive scheduler
 *   18. ata_init()      – ATA PIO primary master
 *   19. fs_init()       – VesperFS check
 *   20. rtl8139_init()  – RTL8139 NIC (optional)
 *   21. net_init()      – network stack (if NIC present)
 *   22. sti             – enable hardware interrupts
 *   23. dhcp_discover() – DHCP lease (if NIC present, max 3 s)
 *   24. shell_run()     – interactive shell (never returns)
 * ---------------------------------------------------------------------- */
void kernel_main(void)
{
    vga_init();

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("================================================================\n");
    vga_puts("              VESPER KERNEL ACTIVE                             \n");
    vga_puts("        Advanced Services Educational OS  v0.5.0               \n");
    vga_puts("================================================================\n");

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("\n");
    vga_puts("  Architecture : x86  32-bit Protected Mode\n");
    vga_puts("  VGA buffer   : 0xB8000\n");
    vga_puts("  Kernel base  : 0x1000\n");
    vga_puts("  Stack base   : 0x90000\n");
    vga_puts("  Heap size    : 256 KB\n");
    vga_puts("\n");

    serial_init();
    serial_puts("VESPER: serial console active (115200 8N1)\n");
    debug_puts("VESPER: kernel main entered\n");

    kmem_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] Heap allocator  (256 KB)\n");

    tss_init(0x90000u);
    gdt_init();
    tss_flush();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] GDT loaded      (6 entries: null/kcode/kdata/ucode/udata/TSS)\n");
    vga_puts("  [OK] TSS installed   (ESP0=0x90000, SS0=0x10)\n");

    idt_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] IDT loaded      (256 gates)\n");

    pic_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] PIC remapped    (IRQ0-15 -> INT 32-47)\n");

    timer_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_printf("  [OK] PIT timer       (IRQ0, 100 Hz, preempt every %u ticks)\n",
               (uint32_t)SCHED_SLICE);

    keyboard_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] Keyboard driver (IRQ1, extended scancodes)\n");

    mouse_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] Mouse driver    (IRQ12, PS/2 3-byte packets)\n");
    serial_puts("VESPER: mouse driver initialized\n");
    debug_puts("VESPER: mouse driver initialized\n");

    pmm_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] PMM ready       (");
    vga_print_uint(pmm_free_frames() * 4u);
    vga_puts(" KB free)\n");

    paging_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] Paging enabled  (identity-map 0-8 MB, per-process PDs)\n");

    syscall_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] Syscall gate    (INT 0x80, DPL=3, syscalls 0-12)\n");

    pipe_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_printf("  [OK] IPC pipes       (%u slots x %u B ring buffer)\n",
               (uint32_t)MAX_PIPES, (uint32_t)PIPE_BUF_SIZE);

    fd_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_printf("  [OK] FD table        (%u slots)\n", (uint32_t)FD_MAX);

    process_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_printf("  [OK] Scheduler       (preemptive round-robin, max %u procs)\n",
               (uint32_t)MAX_PROCESSES);

    if (ata_init()) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("  [OK] ATA drive       (primary master, PIO)\n");
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("  [--] ATA drive       (not detected)\n");
    }

    if (fs_init()) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("  [OK] VesperFS        (mounted at LBA 129)\n");
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("  [--] VesperFS        (not formatted – run 'mkfs')\n");
    }

    int nic_ok = rtl8139_init();
    if (nic_ok) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("  [OK] RTL8139 NIC     (PCI, IRQ-driven RX, 4-slot TX)\n");
        serial_puts("VESPER: rtl8139 detected\n");
        debug_puts("VESPER: rtl8139 detected\n");
        net_init();
        vga_printf("  [OK] Network stack   (MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
                   (uint32_t)net_config.mac[0], (uint32_t)net_config.mac[1],
                   (uint32_t)net_config.mac[2], (uint32_t)net_config.mac[3],
                   (uint32_t)net_config.mac[4], (uint32_t)net_config.mac[5]);
        serial_puts("VESPER: network stack initialized\n");
        debug_puts("VESPER: network stack initialized\n");
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("  [--] RTL8139 NIC     (not detected – networking disabled)\n");
        serial_puts("VESPER: rtl8139 not detected\n");
        debug_puts("VESPER: rtl8139 not detected\n");
    }

    __asm__ volatile ("sti");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  [OK] Interrupts enabled\n");

    if (nic_ok) {
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_puts("  [..] DHCP discovery  (timeout 3 s)...\n");
        serial_puts("VESPER: dhcp discovery start\n");
        debug_puts("VESPER: dhcp discovery start\n");
        if (dhcp_discover(300u)) {
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            vga_printf("  [OK] DHCP lease      (%u.%u.%u.%u)\n",
                       (net_config.ip >> 24) & 0xFFu,
                       (net_config.ip >> 16) & 0xFFu,
                       (net_config.ip >>  8) & 0xFFu,
                        net_config.ip        & 0xFFu);
            serial_puts("VESPER: dhcp lease acquired\n");
            debug_puts("VESPER: dhcp lease acquired\n");
        } else {
            vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            vga_puts("  [--] DHCP lease      (no response – use 'dhcp' to retry)\n");
            serial_puts("VESPER: dhcp lease timeout\n");
            debug_puts("VESPER: dhcp lease timeout\n");
        }
    }

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("\n  Type 'help' for available commands.\n\n");

    shell_run();

    while (1) {
        __asm__ volatile ("hlt");
    }
}
