#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "timer.h"
#include "rtc.h"
#include "process.h"
#include "pipe.h"
#include "fd.h"

/* IDT gate attribute for a DPL=3 interrupt gate (callable from ring 3) */
#define IDT_SYSCALL_GATE  0xEEu   /* P=1 DPL=3 S=0 Type=1110 (32-bit int gate) */

/* Forward declaration of the ASM stub defined in isr.asm */
extern void isr_syscall(void);

/* -------------------------------------------------------------------------
 * User pointer validation
 *
 * Kernel-mode threads (is_user == 0) can pass any pointer.  User-mode
 * processes must only pass addresses within the user address space
 * (USER_ADDR_LOW … USER_ADDR_HIGH) to prevent ring-3 code from tricking
 * the kernel into reading/writing arbitrary kernel memory.
 * ---------------------------------------------------------------------- */
#define USER_ADDR_LOW   0x01000000u   /* USER_LOAD_BASE (from elf.h) */
#define USER_ADDR_HIGH  0x01020000u   /* USER_STACK_TOP (from elf.h) */

/*
 * validate_user_ptr – check that a user-mode buffer [ptr, ptr+len) lies
 * entirely within the valid user address range.
 * Returns 1 if valid, 0 if the pointer is outside the user space.
 * Kernel threads always pass validation (they share the flat address space).
 */
static int validate_user_ptr(const void *ptr, uint32_t len)
{
    if (!current_process || !current_process->is_user) {
        return 1;   /* kernel threads can address anything */
    }
    uint32_t start = (uint32_t)(uintptr_t)ptr;
    uint32_t end   = start + len;
    /* Overflow check + range check */
    if (end < start || start < USER_ADDR_LOW || end > USER_ADDR_HIGH) {
        return 0;
    }
    return 1;
}

/*
 * validate_user_string – check that a NUL-terminated user string starts
 * within user space.  Limits scan to max_len bytes.
 * Returns 1 if valid, 0 otherwise.
 */
static int validate_user_string(const char *str, uint32_t max_len)
{
    if (!current_process || !current_process->is_user) {
        return 1;
    }
    uint32_t addr = (uint32_t)(uintptr_t)str;
    if (addr < USER_ADDR_LOW || addr >= USER_ADDR_HIGH) {
        return 0;
    }
    /* Ensure at least one byte and NUL terminator within bounds */
    uint32_t avail = USER_ADDR_HIGH - addr;
    if (avail > max_len) {
        avail = max_len;
    }
    for (uint32_t i = 0; i < avail; i++) {
        if (str[i] == '\0') {
            return 1;
        }
    }
    return 0;   /* no NUL within valid range */
}

void syscall_init(void)
{
    idt_set_gate(0x80u, (uint32_t)isr_syscall, 0x08u, IDT_SYSCALL_GATE);
}

/* -------------------------------------------------------------------------
 * syscall_handler – dispatched from the INT 0x80 stub in isr.asm.
 *
 * @regs : full saved register frame.  Fields used:
 *   regs->eax = syscall number (on entry) / return value (on exit)
 *   regs->ebx = arg0
 *   regs->ecx = arg1
 *   regs->edx = arg2
 *
 * The handler writes the return value into regs->eax; POPA in the stub
 * then restores that value into the caller's EAX register.
 * ---------------------------------------------------------------------- */
void syscall_handler(registers_t *regs)
{
    uint32_t num = regs->eax;
    uint32_t ebx = regs->ebx;
    uint32_t ecx = regs->ecx;
    uint32_t edx = regs->edx;

    /* Default return value: 0 (success) */
    regs->eax = 0u;

    switch (num) {

    /* ------------------------------------------------------------------ */
    case SYS_EXIT:
        /* Terminate calling process */
        process_exit();
        break;

    /* ------------------------------------------------------------------ */
    case SYS_WRITE:
        /*
         * Write ECX bytes from buffer at EBX to VGA stdout.
         * EBX = buffer address, ECX = byte count
         * Returns bytes written in EAX.
         */
        {
            const char *buf = (const char *)(uintptr_t)ebx;
            uint32_t    len = ecx;
            if (!validate_user_ptr(buf, len)) {
                regs->eax = (uint32_t)-1;
                break;
            }
            for (uint32_t i = 0; i < len; i++) {
                vga_putchar(buf[i]);
            }
            regs->eax = len;
        }
        break;

    /* ------------------------------------------------------------------ */
    case SYS_GETPID:
        /* Return current process PID */
        regs->eax = current_process ? current_process->pid : 0u;
        break;

    /* ------------------------------------------------------------------ */
    case SYS_SLEEP:
        /* Sleep EBX milliseconds */
        timer_sleep_ms(ebx);
        break;

    /* ------------------------------------------------------------------ */
    case SYS_YIELD:
        /* Voluntarily yield the CPU to the next ready process */
        process_yield();
        break;

    /* ------------------------------------------------------------------ */
    case SYS_KILL:
        /*
         * Terminate process with PID EBX.
         * Returns 0 on success, (uint32_t)-1 on failure.
         */
        regs->eax = (uint32_t)process_kill(ebx);
        break;

    /* ------------------------------------------------------------------ */
    case SYS_OPEN:
        /*
         * Open a VesperFS file by name pointer EBX.
         * Returns fd (≥ 0) or (uint32_t)-1 on error.
         */
        {
            const char *name = (const char *)(uintptr_t)ebx;
            if (!validate_user_string(name, 256u)) {
                regs->eax = (uint32_t)-1;
                break;
            }
            uint32_t    pid  = current_process ? current_process->pid : 0u;
            regs->eax = (uint32_t)fd_open(name, pid);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SYS_READ:
        /*
         * Read from file descriptor EBX.
         * EBX = fd, ECX = max bytes, EDX = buffer address
         * Returns bytes read, 0 at EOF, or (uint32_t)-1 on error.
         */
        {
            int      fd  = (int)ebx;
            uint32_t len = ecx;
            void    *buf = (void *)(uintptr_t)edx;
            if (!validate_user_ptr(buf, len)) {
                regs->eax = (uint32_t)-1;
                break;
            }
            uint32_t pid = current_process ? current_process->pid : 0u;
            regs->eax = (uint32_t)fd_read(fd, buf, len, pid);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SYS_CLOSE:
        /* Close file descriptor EBX */
        {
            uint32_t pid = current_process ? current_process->pid : 0u;
            fd_close((int)ebx, pid);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SYS_GETTIME:
        /*
         * Return seconds since midnight (RTC).
         * EAX = hour*3600 + minute*60 + second.
         */
        {
            rtc_time_t t;
            rtc_read(&t);
            regs->eax = (uint32_t)t.hour * 3600u
                      + (uint32_t)t.minute * 60u
                      + (uint32_t)t.second;
        }
        break;

    /* ------------------------------------------------------------------ */
    case SYS_PIPE_CREATE:
        /* Allocate a new pipe; returns pipe_id or (uint32_t)-1 */
        regs->eax = (uint32_t)pipe_alloc();
        break;

    /* ------------------------------------------------------------------ */
    case SYS_PIPE_WRITE:
        /*
         * Write to a pipe.
         * EBX = pipe_id, ECX = byte count, EDX = buffer address
         * Returns bytes written or (uint32_t)-1 on error.
         */
        {
            int         pipe_id = (int)ebx;
            const void *buf     = (const void *)(uintptr_t)edx;
            if (!validate_user_ptr(buf, ecx)) {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)pipe_write(pipe_id, buf, ecx);
        }
        break;

    /* ------------------------------------------------------------------ */
    case SYS_PIPE_READ:
        /*
         * Read from a pipe (blocks if empty).
         * EBX = pipe_id, ECX = max bytes, EDX = buffer address
         * Returns bytes read or (uint32_t)-1 on error.
         */
        {
            int   pipe_id = (int)ebx;
            void *buf     = (void *)(uintptr_t)edx;
            if (!validate_user_ptr(buf, ecx)) {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)pipe_read(pipe_id, buf, ecx);
        }
        break;

    /* ------------------------------------------------------------------ */
    default:
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("[syscall] unknown: ");
        vga_print_uint(num);
        vga_putchar('\n');
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        regs->eax = (uint32_t)-1;
        break;
    }
}
