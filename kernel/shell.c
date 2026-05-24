#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "mouse.h"
#include "kmem.h"
#include "port_io.h"
#include "timer.h"
#include "pmm.h"
#include "process.h"
#include "paging.h"
#include "ata.h"
#include "fs.h"
#include "elf.h"
#include "rtc.h"
#include "net.h"
#include "string.h"

#define NULL ((void *)0)

/* Shell prompt string */
#define SHELL_PROMPT    "vesper> "

/* Maximum number of characters accepted per command line */
#define SHELL_BUF_SIZE  256

/* Command history */
#define HISTORY_SIZE    8

/* -------------------------------------------------------------------------
 * Minimal string utilities  (no libc available)
 * ---------------------------------------------------------------------- */

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int str_startswith(const char *str, const char *prefix)
{
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++; prefix++;
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * Built-in commands
 * ---------------------------------------------------------------------- */

static void cmd_help(void)
{
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("VESPER OS Shell  -  Available commands:\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("  help             Show this help message\n");
    vga_puts("  clear            Clear the screen\n");
    vga_puts("  echo <text>      Print text to the screen\n");
    vga_puts("  version          Show OS version information\n");
    vga_puts("  meminfo          Show heap + physical memory statistics\n");
    vga_puts("  uptime           Show system uptime\n");
    vga_puts("  date             Show current date and time (RTC)\n");
    vga_puts("  ps               List running processes\n");
    vga_puts("  kill <pid>       Terminate a process by PID\n");
    vga_puts("  sleep <ms>       Sleep for N milliseconds\n");
    vga_puts("  colortest        Display all 16 VGA colours\n");
    vga_puts("  mouse            Show PS/2 mouse position and button state\n");
    vga_puts("  netinfo          Show network configuration (IP, MAC, gateway)\n");
    vga_puts("  arp              Show ARP cache (IP-to-MAC mappings)\n");
    vga_puts("  dhcp             Re-run DHCP discovery to obtain an IP address\n");
    vga_puts("  mkfs             Format the VesperFS partition\n");
    vga_puts("  ls               List files in VesperFS\n");
    vga_puts("  cat <file>       Print file contents\n");
    vga_puts("  write <f> <txt>  Write text to a file\n");
    vga_puts("  rm <file>        Delete a file from VesperFS\n");
    vga_puts("  run <file>       Load ELF32; spawn as kernel thread\n");
    vga_puts("  exec <file>      Load ELF32; spawn as ring-3 user process\n");
    vga_puts("  halt             Halt the CPU\n");
    vga_puts("  reboot           Reboot the system\n");
    vga_puts("\n");
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_puts("  Up/Down arrows cycle through command history.\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void cmd_clear(void)
{
    vga_clear();
}

static void cmd_echo(const char *args)
{
    vga_puts(args);
    vga_putchar('\n');
}

static void cmd_version(void)
{
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts("VESPER OS  v0.6.0\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("Architecture   : x86 32-bit Protected Mode\n");
    vga_puts("VGA buffer     : 0xB8000\n");
    vga_puts("Kernel base    : 0x1000\n");
    vga_puts("Interrupt model: IDT + 8259A PIC (IRQ0-15 -> INT 32-47)\n");
    vga_puts("Keyboard       : PS/2 interrupt-driven (IRQ1, extended scancodes)\n");
    vga_puts("Mouse          : PS/2 interrupt-driven (IRQ12, 3-byte packets)\n");
    vga_puts("Timer          : PIT 100 Hz (IRQ0), preempt every 50 ms\n");
    vga_puts("Memory         : PMM bitmap + 256 KB heap + paging (0-8 MB)\n");
    vga_puts("Scheduler      : preemptive round-robin (50 ms slice)\n");
    vga_puts("Page dirs      : per-process (user processes get isolated PDs)\n");
    vga_puts("Syscalls       : INT 0x80 (DPL=3), 13 syscalls (0-12)\n");
    vga_puts("File I/O       : SYS_OPEN/READ/CLOSE via global FD table\n");
    vga_puts("IPC            : ring-buffer pipes (SYS_PIPE_CREATE/WRITE/READ)\n");
    vga_puts("RTC            : CMOS real-time clock\n");
    vga_puts("Disk           : ATA PIO (primary master)\n");
    vga_puts("Filesystem     : VesperFS (LBA 129+), supports create/read/delete\n");
    vga_puts("ELF loader     : ELF32 (kernel threads + ring-3 user processes)\n");
    vga_puts("NIC            : RTL8139 (PCI, IRQ-driven RX, polled TX)\n");
    vga_puts("Network        : Ethernet + ARP + IPv4 + UDP + DHCP client\n");
}

static void cmd_meminfo(void)
{
    uint32_t used, free_bytes, total;
    kmem_info(&used, &free_bytes, &total);

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("Heap Memory:\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("  Total  : ");
    vga_print_uint(total / 1024u);
    vga_puts(" KB (");
    vga_print_uint(total);
    vga_puts(" bytes)\n");
    vga_puts("  Used   : ");
    vga_print_uint(used);
    vga_puts(" bytes\n");
    vga_puts("  Free   : ");
    vga_print_uint(free_bytes);
    vga_puts(" bytes\n");

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("Physical Memory (PMM):\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("  Total  : ");
    vga_print_uint(pmm_total_frames() * 4u);
    vga_puts(" KB (");
    vga_print_uint(pmm_total_frames());
    vga_puts(" frames)\n");
    vga_puts("  Free   : ");
    vga_print_uint(pmm_free_frames() * 4u);
    vga_puts(" KB (");
    vga_print_uint(pmm_free_frames());
    vga_puts(" frames)\n");
}

static void cmd_halt(void)
{
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts("CPU halted. Reset to continue.\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    __asm__ volatile ("cli; hlt");
    while (1) {}
}

static void cmd_reboot(void)
{
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts("Rebooting...\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    /* Pulse the keyboard controller reset line (port 0x64 bit 0) */
    outb(0x64, 0xFE);
    /* If that fails, spin */
    while (1) {}
}

static void cmd_uptime(void)
{
    uint32_t secs  = timer_get_uptime_sec();
    uint32_t mins  = secs / 60u;
    uint32_t hours = mins  / 60u;
    secs  %= 60u;
    mins  %= 60u;

    vga_puts("Uptime: ");
    vga_print_uint(hours);
    vga_puts("h ");
    vga_print_uint(mins);
    vga_puts("m ");
    vga_print_uint(secs);
    vga_puts("s  (");
    vga_print_uint(timer_get_ticks());
    vga_puts(" ticks at 100 Hz)\n");
}

static void cmd_ps(void)
{
    process_print_all();
}

static void cmd_mkfs(void)
{
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts("Formatting VesperFS at LBA 129... ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    if (fs_format() == 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("done.\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("FAILED (disk write error).\n");
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void cmd_ls(void)
{
    if (!fs_init()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("No VesperFS found. Run 'mkfs' first.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    fs_ls();
}

static void cmd_cat(const char *name)
{
    if (name[0] == '\0') {
        vga_puts("Usage: cat <filename>\n");
        return;
    }
    if (!fs_init()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("No VesperFS found. Run 'mkfs' first.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    fs_file_t f;
    if (fs_open(&f, name) != 0) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("File not found: ");
        vga_puts(name);
        vga_putchar('\n');
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    /* Read and print 128 bytes at a time */
    char buf[129];
    uint32_t n;
    while ((n = fs_read(&f, buf, 128u)) > 0) {
        buf[n] = '\0';
        vga_puts(buf);
    }
    fs_close(&f);
    vga_putchar('\n');
}

/*
 * cmd_write – write a text argument directly to a named file.
 *
 * Usage: write <filename> <text...>
 *
 * The filename must not contain spaces.  Everything after the second
 * space-separated token is stored verbatim as the file content.
 */
static void cmd_write(const char *args)
{
    /* Extract filename (first token) */
    char fname[FS_NAME_MAX + 1];
    uint32_t i = 0;
    while (args[i] && args[i] != ' ' && i < FS_NAME_MAX) {
        fname[i] = args[i];
        i++;
    }
    fname[i] = '\0';

    if (fname[0] == '\0') {
        vga_puts("Usage: write <filename> <text>\n");
        return;
    }

    /* Skip spaces between name and content */
    while (args[i] == ' ') {
        i++;
    }

    const char *content = args + i;
    uint32_t    len     = strlen(content);

    if (!fs_init()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("No VesperFS. Run 'mkfs' first.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    if (fs_write_new(fname, content, len) == 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("Written: ");
        vga_puts(fname);
        vga_puts(" (");
        vga_print_uint(len);
        vga_puts(" bytes)\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Write failed.\n");
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

/*
 * cmd_run – load an ELF32 file from VesperFS into memory and execute it
 *           as a new kernel thread.
 */

/* Scratch buffer for loading ELF data – static to avoid large stack use */
#define RUN_BUF_SIZE (64u * 1024u)   /* 64 KB – sufficient for small programs */
static uint8_t run_buf[RUN_BUF_SIZE];

static void cmd_run(const char *name)
{
    if (name[0] == '\0') {
        vga_puts("Usage: run <filename>\n");
        return;
    }
    if (!fs_init()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("No VesperFS. Run 'mkfs' first.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    fs_file_t f;
    if (fs_open(&f, name) != 0) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("File not found: ");
        vga_puts(name);
        vga_putchar('\n');
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    if (f.size > RUN_BUF_SIZE) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("File too large (max 64 KB).\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fs_close(&f);
        return;
    }

    uint32_t nread = fs_read(&f, run_buf, f.size);
    fs_close(&f);

    uint32_t entry = elf_load(run_buf, nread);
    if (entry == 0) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Not a valid ELF32 binary.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    /* Cast the entry point to a function and create a kernel thread for it */
    void (*fn)(void) = (void (*)(void))entry;
    process_t *p = process_create(name, fn);
    if (!p) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Process table full.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("Spawned PID ");
    vga_print_uint(p->pid);
    vga_puts(": ");
    vga_puts(name);
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Yield once so the new process gets a chance to run */
    process_yield();
}

static void cmd_date(void)
{
    rtc_time_t t;
    rtc_read(&t);

    /* Pad single-digit values with a leading zero */
    vga_printf("Date: %u-%02u-%02u  Time: %02u:%02u:%02u\n",
               (uint32_t)t.year,
               (uint32_t)t.month,
               (uint32_t)t.day,
               (uint32_t)t.hour,
               (uint32_t)t.minute,
               (uint32_t)t.second);
}

static void cmd_kill(const char *args)
{
    /* Parse PID from args */
    uint32_t pid = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10u + (uint32_t)(*p - '0');
        p++;
    }

    if (pid == 0) {
        vga_puts("Usage: kill <pid>\n");
        return;
    }

    if (process_kill(pid) == 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("Process ");
        vga_print_uint(pid);
        vga_puts(" terminated.\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("No such process: PID ");
        vga_print_uint(pid);
        vga_putchar('\n');
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void cmd_sleep(const char *args)
{
    uint32_t ms = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9') {
        ms = ms * 10u + (uint32_t)(*p - '0');
        p++;
    }

    if (ms == 0) {
        vga_puts("Usage: sleep <milliseconds>\n");
        return;
    }

    vga_puts("Sleeping ");
    vga_print_uint(ms);
    vga_puts(" ms...\n");
    timer_sleep_ms(ms);
    vga_puts("Done.\n");
}

static void cmd_colortest(void)
{
    static const char * const color_names[] = {
        "BLACK   ", "BLUE    ", "GREEN   ", "CYAN    ",
        "RED     ", "MAGENTA ", "BROWN   ", "LT_GREY ",
        "DK_GREY ", "LT_BLUE ", "LT_GREEN", "LT_CYAN ",
        "LT_RED  ", "LT_MAG  ", "YELLOW  ", "WHITE   "
    };

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("VGA colour palette (foreground on black):\n");

    for (int i = 0; i < 16; i++) {
        vga_set_color((vga_color_t)i, VGA_COLOR_BLACK);
        vga_puts("  ");
        vga_puts(color_names[i]);
        if ((i & 1) == 1) {
            vga_putchar('\n');
        }
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

static void cmd_rm(const char *name)
{
    if (name[0] == '\0') {
        vga_puts("Usage: rm <filename>\n");
        return;
    }
    if (!fs_init()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("No VesperFS. Run 'mkfs' first.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    if (fs_delete(name) == 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("Deleted: ");
        vga_puts(name);
        vga_putchar('\n');
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("File not found: ");
        vga_puts(name);
        vga_putchar('\n');
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

/*
 * cmd_exec – load an ELF32 binary from VesperFS into an isolated user-mode
 *            page directory and spawn it as a ring-3 process.
 *
 * The ELF binary must be linked with:
 *   - entry at or near USER_LOAD_BASE (0x01000000)
 *   - available stack at USER_STACK_TOP (0x01020000) growing downward
 */
static void cmd_exec(const char *name)
{
    if (name[0] == '\0') {
        vga_puts("Usage: exec <filename>\n");
        return;
    }
    if (!fs_init()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("No VesperFS. Run 'mkfs' first.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    fs_file_t f;
    if (fs_open(&f, name) != 0) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("File not found: ");
        vga_puts(name);
        vga_putchar('\n');
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    if (f.size > RUN_BUF_SIZE) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("File too large (max 64 KB).\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fs_close(&f);
        return;
    }

    uint32_t nread = fs_read(&f, run_buf, f.size);
    fs_close(&f);

    /* Allocate a page directory for the new user process */
    uint32_t pd_phys = paging_create_pd();
    if (!pd_phys) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Out of physical memory.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    /* Load ELF segments into the user PD with PAGE_USER */
    uint32_t entry = elf_load_user(run_buf, nread, pd_phys);
    if (entry == 0u) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Not a valid ELF32 binary.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        paging_destroy_pd(pd_phys);
        return;
    }

    /* Allocate a user-mode stack (grows downward from USER_STACK_TOP) */
    uint32_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * 4096u;
    if (paging_alloc_user_pages(pd_phys, stack_base, USER_STACK_PAGES) != 0) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Failed to allocate user stack.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        paging_destroy_pd(pd_phys);
        return;
    }

    process_t *p = process_create_user(name, entry, USER_STACK_TOP, pd_phys);
    if (!p) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Process table full.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        paging_destroy_pd(pd_phys);
        return;
    }

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("Spawned user-mode PID ");
    vga_print_uint(p->pid);
    vga_puts(": ");
    vga_puts(name);
    vga_puts(" (entry=");
    vga_print_hex(entry);
    vga_puts(")\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    process_yield();
}

static void cmd_mouse(void)
{
    mouse_state_t s = mouse_get_state();
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("PS/2 Mouse State:\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("  Position : col=");
    vga_print_uint((uint32_t)(int32_t)s.x);
    vga_puts("  row=");
    vga_print_uint((uint32_t)(int32_t)s.y);
    vga_putchar('\n');
    vga_puts("  Buttons  : ");
    vga_puts((s.buttons & 0x01u) ? "LEFT "  : "     ");
    vga_puts((s.buttons & 0x02u) ? "RIGHT " : "      ");
    vga_puts((s.buttons & 0x04u) ? "MIDDLE" : "      ");
    vga_putchar('\n');
    vga_puts("  Packets  : ");
    vga_print_uint(s.packets);
    vga_putchar('\n');
}

static void cmd_netinfo(void)
{
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("Network Configuration:\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_printf("  MAC      : %02x:%02x:%02x:%02x:%02x:%02x\n",
               (uint32_t)net_config.mac[0], (uint32_t)net_config.mac[1],
               (uint32_t)net_config.mac[2], (uint32_t)net_config.mac[3],
               (uint32_t)net_config.mac[4], (uint32_t)net_config.mac[5]);
    if (net_config.configured) {
        vga_printf("  IP       : %u.%u.%u.%u\n",
                   (net_config.ip      >> 24) & 0xFFu,
                   (net_config.ip      >> 16) & 0xFFu,
                   (net_config.ip      >>  8) & 0xFFu,
                    net_config.ip             & 0xFFu);
        vga_printf("  Netmask  : %u.%u.%u.%u\n",
                   (net_config.netmask >> 24) & 0xFFu,
                   (net_config.netmask >> 16) & 0xFFu,
                   (net_config.netmask >>  8) & 0xFFu,
                    net_config.netmask        & 0xFFu);
        vga_printf("  Gateway  : %u.%u.%u.%u\n",
                   (net_config.gateway >> 24) & 0xFFu,
                   (net_config.gateway >> 16) & 0xFFu,
                   (net_config.gateway >>  8) & 0xFFu,
                    net_config.gateway        & 0xFFu);
        vga_printf("  DNS      : %u.%u.%u.%u\n",
                   (net_config.dns     >> 24) & 0xFFu,
                   (net_config.dns     >> 16) & 0xFFu,
                   (net_config.dns     >>  8) & 0xFFu,
                    net_config.dns            & 0xFFu);
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("  Status   : not configured (no DHCP lease)\n");
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void cmd_arp(void)
{
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("ARP Cache:\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    net_print_arp_cache();
}

static void cmd_dhcp(void)
{
    if (net_config.mac[0] == 0u && net_config.mac[1] == 0u &&
        net_config.mac[2] == 0u && net_config.mac[3] == 0u &&
        net_config.mac[4] == 0u && net_config.mac[5] == 0u) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("No NIC detected. DHCP unavailable.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    vga_puts("Running DHCP discovery (timeout 3 s)...\n");
    if (dhcp_discover(300u)) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_printf("Lease obtained: %u.%u.%u.%u\n",
                   (net_config.ip >> 24) & 0xFFu,
                   (net_config.ip >> 16) & 0xFFu,
                   (net_config.ip >>  8) & 0xFFu,
                    net_config.ip        & 0xFFu);
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("DHCP timed out - no lease obtained.\n");
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void cmd_unknown(const char *cmd)
{
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    vga_puts("Unknown command: ");
    vga_puts(cmd);
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("Type 'help' for a list of available commands.\n");
}

/* -------------------------------------------------------------------------
 * Input / dispatch helpers
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Command history
 * ---------------------------------------------------------------------- */
static char history[HISTORY_SIZE][SHELL_BUF_SIZE];
static int  history_count = 0;   /* total entries ever added */

static void history_push(const char *cmd)
{
    if (cmd[0] == '\0') {
        return;
    }
    int slot = history_count % HISTORY_SIZE;
    strncpy(history[slot], cmd, SHELL_BUF_SIZE - 1);
    history[slot][SHELL_BUF_SIZE - 1] = '\0';
    history_count++;
}

/* history_get(1) = most recent, history_get(2) = one before that, etc.
 * Returns NULL if index is out of range (never entered yet). */
static const char *history_get(int back)
{
    if (back <= 0 || back > history_count || back > HISTORY_SIZE) {
        return NULL;
    }
    int slot = ((history_count - back) % HISTORY_SIZE + HISTORY_SIZE) % HISTORY_SIZE;
    return history[slot];
}

/* -------------------------------------------------------------------------
 * Input helpers: erase displayed characters back to start of input
 * ---------------------------------------------------------------------- */
static void erase_input(int len)
{
    for (int i = 0; i < len; i++) {
        vga_putchar('\b');
    }
}

/* -------------------------------------------------------------------------
 * read_line – read a line from the keyboard with history browsing
 *
 * Up arrow  → recall older entry
 * Down arrow → recall newer entry (or clear if past newest)
 * Backspace → delete one character
 * Enter     → submit
 * ---------------------------------------------------------------------- */
static void read_line(char *buf, int max_len)
{
    int len       = 0;
    int hist_pos  = 0;   /* 0 = current (blank), 1 = newest, 2 = next older … */

    while (len < max_len - 1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            vga_putchar('\n');
            break;

        } else if (c == '\b') {
            if (len > 0) {
                len--;
                vga_putchar('\b');
            }

        } else if ((unsigned char)c == KEY_UP) {
            /* Scroll backwards through history */
            int next_pos = hist_pos + 1;
            const char *entry = history_get(next_pos);
            if (entry) {
                erase_input(len);
                strncpy(buf, entry, (uint32_t)(max_len - 1));
                buf[max_len - 1] = '\0';
                len = (int)strlen(buf);
                vga_puts(buf);
                hist_pos = next_pos;
            }

        } else if ((unsigned char)c == KEY_DOWN) {
            /* Scroll forwards (towards present) */
            if (hist_pos > 0) {
                erase_input(len);
                hist_pos--;
                if (hist_pos == 0) {
                    /* Returned to blank current line */
                    buf[0] = '\0';
                    len    = 0;
                } else {
                    const char *entry = history_get(hist_pos);
                    if (entry) {
                        strncpy(buf, entry, (uint32_t)(max_len - 1));
                        buf[max_len - 1] = '\0';
                        len = (int)strlen(buf);
                        vga_puts(buf);
                    }
                }
            }

        } else if ((unsigned char)c >= 0x20u && (unsigned char)c < 0x80u) {
            /* Printable ASCII */
            buf[len++] = c;
            vga_putchar(c);
        }
        /* Ignore other control characters / non-printable KEY_* constants */
    }

    buf[len] = '\0';
}

static void execute_command(char *cmd)
{
    /* Skip leading spaces */
    while (*cmd == ' ') cmd++;

    if (*cmd == '\0') return;   /* blank line */

    if (str_eq(cmd, "help")) {
        cmd_help();
    } else if (str_eq(cmd, "clear")) {
        cmd_clear();
    } else if (str_startswith(cmd, "echo ")) {
        cmd_echo(cmd + 5);
    } else if (str_eq(cmd, "echo")) {
        vga_putchar('\n');
    } else if (str_eq(cmd, "version")) {
        cmd_version();
    } else if (str_eq(cmd, "meminfo")) {
        cmd_meminfo();
    } else if (str_eq(cmd, "uptime")) {
        cmd_uptime();
    } else if (str_eq(cmd, "date")) {
        cmd_date();
    } else if (str_eq(cmd, "ps")) {
        cmd_ps();
    } else if (str_startswith(cmd, "kill ")) {
        cmd_kill(cmd + 5);
    } else if (str_eq(cmd, "kill")) {
        vga_puts("Usage: kill <pid>\n");
    } else if (str_startswith(cmd, "sleep ")) {
        cmd_sleep(cmd + 6);
    } else if (str_eq(cmd, "sleep")) {
        vga_puts("Usage: sleep <milliseconds>\n");
    } else if (str_eq(cmd, "colortest")) {
        cmd_colortest();
    } else if (str_eq(cmd, "mouse")) {
        cmd_mouse();
    } else if (str_eq(cmd, "netinfo")) {
        cmd_netinfo();
    } else if (str_eq(cmd, "arp")) {
        cmd_arp();
    } else if (str_eq(cmd, "dhcp")) {
        cmd_dhcp();
    } else if (str_eq(cmd, "mkfs")) {
        cmd_mkfs();
    } else if (str_eq(cmd, "ls")) {
        cmd_ls();
    } else if (str_startswith(cmd, "cat ")) {
        cmd_cat(cmd + 4);
    } else if (str_eq(cmd, "cat")) {
        vga_puts("Usage: cat <filename>\n");
    } else if (str_startswith(cmd, "write ")) {
        cmd_write(cmd + 6);
    } else if (str_eq(cmd, "write")) {
        vga_puts("Usage: write <filename> <text>\n");
    } else if (str_startswith(cmd, "rm ")) {
        cmd_rm(cmd + 3);
    } else if (str_eq(cmd, "rm")) {
        vga_puts("Usage: rm <filename>\n");
    } else if (str_startswith(cmd, "run ")) {
        cmd_run(cmd + 4);
    } else if (str_eq(cmd, "run")) {
        vga_puts("Usage: run <filename>\n");
    } else if (str_startswith(cmd, "exec ")) {
        cmd_exec(cmd + 5);
    } else if (str_eq(cmd, "exec")) {
        vga_puts("Usage: exec <filename>\n");
    } else if (str_eq(cmd, "halt")) {
        cmd_halt();
    } else if (str_eq(cmd, "reboot")) {
        cmd_reboot();
    } else {
        cmd_unknown(cmd);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void shell_init(void)
{
    /* keyboard_init() is now called directly from kernel_main() before STI */
}

void shell_run(void)
{
    char buf[SHELL_BUF_SIZE];

    while (1) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts(SHELL_PROMPT);
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        read_line(buf, SHELL_BUF_SIZE);
        history_push(buf);
        execute_command(buf);
    }
}
