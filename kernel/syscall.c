#include "elf.h"
#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "timer.h"
#include "rtc.h"
#include "process.h"
#include "pipe.h"
#include "fd.h"
#include "paging.h"

/* IDT gate attribute for a DPL=3 interrupt gate (callable from ring 3) */
#define IDT_SYSCALL_GATE  0xEEu   /* P=1 DPL=3 S=0 Type=1110 (32-bit int gate) */

/* Forward declaration of the ASM stub defined in isr.asm */
extern void isr_syscall(void);

static int syscall_is_user_context(void)
{
    return current_process && current_process->is_user && current_process->pd_phys;
}

static int syscall_user_buffer_ok(uint32_t ptr, uint32_t len, int write_required)
{
    if (!syscall_is_user_context()) {
        return 1;
    }
    return paging_user_range_accessible(current_process->pd_phys, ptr, len,
                                        write_required);
}

static int syscall_user_string_ok(uint32_t ptr, uint32_t max_len)
{
    if (!syscall_is_user_context()) {
        return 1;
    }
    if (ptr < USER_LOAD_BASE || ptr >= USER_STACK_TOP) {
        return 0;
    }

    for (uint32_t i = 0; i < max_len; i++) {
        uint32_t addr = ptr + i;
        if (addr < ptr || addr >= USER_STACK_TOP) {
            return 0;
        }
        if (!paging_user_range_accessible(current_process->pd_phys, addr, 1u, 0)) {
            return 0;
        }
        if (*(const char *)(uintptr_t)addr == '\0') {
            return 1;
        }
    }

    return 0;
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

    regs->eax = SYS_ERR_INVAL;

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
            if (!syscall_user_buffer_ok(ebx, len, 0)) {
                regs->eax = SYS_ERR_FAULT;
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
        regs->eax = 0u;
        break;

    /* ------------------------------------------------------------------ */
    case SYS_YIELD:
        /* Voluntarily yield the CPU to the next ready process */
        process_yield();
        regs->eax = 0u;
        break;

    /* ------------------------------------------------------------------ */
    case SYS_KILL:
        /*
         * Terminate process with PID EBX.
         * Returns 0 on success, (uint32_t)-1 on failure.
         */
        {
            int rc = process_kill(ebx);
            if (rc == PROCESS_KILL_ERR_FORBIDDEN) {
                regs->eax = SYS_ERR_PERM;
            } else if (rc == PROCESS_KILL_ERR_NOT_FOUND) {
                regs->eax = SYS_ERR_NOENT;
            } else {
                regs->eax = 0u;
            }
        }
        break;

    /* ------------------------------------------------------------------ */
    case SYS_OPEN:
        /*
         * Open a VesperFS file by name pointer EBX.
         * Returns fd (≥ 0) or (uint32_t)-1 on error.
         */
        {
            const char *name = (const char *)(uintptr_t)ebx;
            uint32_t    pid  = current_process ? current_process->pid : 0u;
            if (!name || !syscall_user_string_ok(ebx, FS_NAME_MAX + 1u)) {
                regs->eax = SYS_ERR_FAULT;
                break;
            }

            int rc = fd_open(name, pid);
            if (rc == FD_ERR_TABLE_FULL) {
                regs->eax = SYS_ERR_MFILE;
            } else if (rc == FD_ERR_NOT_FOUND) {
                regs->eax = SYS_ERR_NOENT;
            } else {
                regs->eax = (uint32_t)rc;
            }
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
            uint32_t pid = current_process ? current_process->pid : 0u;
            if (!syscall_user_buffer_ok(edx, len, 1)) {
                regs->eax = SYS_ERR_FAULT;
                break;
            }

            int rc = fd_read(fd, buf, len, pid);
            regs->eax = (rc == FD_ERR_INVALID) ? SYS_ERR_BADF : (uint32_t)rc;
        }
        break;

    /* ------------------------------------------------------------------ */
    case SYS_CLOSE:
        /* Close file descriptor EBX */
        {
            uint32_t pid = current_process ? current_process->pid : 0u;
            int rc = fd_close((int)ebx, pid);
            regs->eax = (rc == 0) ? 0u : SYS_ERR_BADF;
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
        {
            int rc = pipe_alloc();
            regs->eax = (rc >= 0) ? (uint32_t)rc : SYS_ERR_MFILE;
        }
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
            if (!syscall_user_buffer_ok(edx, ecx, 0)) {
                regs->eax = SYS_ERR_FAULT;
                break;
            }

            int rc = pipe_write(pipe_id, buf, ecx);
            regs->eax = (rc >= 0) ? (uint32_t)rc : SYS_ERR_INVAL;
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
            if (!syscall_user_buffer_ok(edx, ecx, 1)) {
                regs->eax = SYS_ERR_FAULT;
                break;
            }

            int rc = pipe_read(pipe_id, buf, ecx);
            regs->eax = (rc >= 0) ? (uint32_t)rc : SYS_ERR_INVAL;
        }
        break;

    /* ------------------------------------------------------------------ */
    default:
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("[syscall] unknown: ");
        vga_print_uint(num);
        vga_putchar('\n');
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        regs->eax = SYS_ERR_NOSYS;
        break;
    }
}
