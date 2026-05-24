#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

/*
 * VESPER OS – Preemptive + co-operative process scheduler (Tier 2)
 *
 * Scheduling model
 * ----------------
 * Kernel threads may explicitly yield (co-operative) via process_yield().
 * The PIT timer (100 Hz) also triggers a preemptive yield every SCHED_SLICE
 * ticks (configurable), giving every process a fair time slice.
 *
 * Context-switch mechanism (process.asm)
 * ----------------------------------------
 * process_yield() pushes the callee-saved registers (EBX, ESI, EDI, EBP)
 * and the return address onto the calling process's kernel stack, then:
 *   1. Calls sched_next() → returns the next process_t*.
 *   2. Calls process_before_switch(next) → updates TSS.ESP0 and CR3.
 *   3. Switches ESP to next->esp (stack swap).
 *   4. Pops the next process's callee-saved registers and RETs.
 *
 * User-mode processes
 * --------------------
 * A user process is created via process_create_user().  Its initial kernel
 * stack has "user_mode_enter" as the return address.  When process_yield
 * first schedules it, the RET lands in user_mode_enter (defined in
 * process.asm), which builds an IRET frame and transitions to ring 3.
 *
 * struct layout REQUIREMENTS
 * ---------------------------
 *   esp     MUST stay at offset 0  (process.asm accesses it without offset)
 *   pd_phys MUST stay at offset 4  (process.asm reads it for user_mode_enter)
 *   is_user MUST stay at offset 16 (user_mode_enter reads it)
 *   user_eip MUST stay at offset 20
 *   user_esp MUST stay at offset 24
 */

#define MAX_PROCESSES       16u
#define PROCESS_KSTACK_SIZE 8192u   /* 8 KB kernel stack per process */
#define SCHED_SLICE         5u      /* preempt after this many timer ticks (50 ms) */

#define PROCESS_KILL_ERR_NOT_FOUND  (-1)
#define PROCESS_KILL_ERR_FORBIDDEN  (-2)

typedef enum {
    PROC_UNUSED  = 0,
    PROC_READY   = 1,
    PROC_RUNNING = 2,
    PROC_BLOCKED = 3,
    PROC_ZOMBIE  = 4,
} proc_state_t;

typedef struct process {
    /* ---- fields accessed by process.asm (keep offsets stable) ---- */
    uint32_t      esp;           /* offset  0: saved kernel stack pointer        */
    uint32_t      pd_phys;       /* offset  4: physical address of page directory
                                  *            (0 = use kernel PD)               */
    proc_state_t  state;         /* offset  8                                    */
    uint32_t      pid;           /* offset 12                                    */
    int           is_user;       /* offset 16: 1 = user-mode process             */
    uint32_t      user_eip;      /* offset 20: ring-3 entry point                */
    uint32_t      user_esp;      /* offset 24: ring-3 stack pointer              */
    /* ---- remaining fields (C only) -------------------------------- */
    char          name[32];      /* offset 28                                    */
    uint8_t       kstack[PROCESS_KSTACK_SIZE];  /* offset 60                    */
} process_t;

/* Pointer to the currently executing process */
extern process_t *current_process;

/* Initialise the process table and create the idle process */
void process_init(void);

/* Create a kernel-mode thread.  Returns NULL if the table is full. */
process_t *process_create(const char *name, void (*entry)(void));

/*
 * Create a user-mode (ring-3) process.
 *
 * @name      : process name (displayed by 'ps')
 * @user_eip  : virtual address of the user-mode entry point
 * @user_esp  : top of the user-mode stack (virtual address)
 *
 * The caller must have already:
 *   - allocated a page directory with paging_create_pd()
 *   - mapped the code and stack pages with paging_alloc_user_pages()
 *
 * Returns a pointer to the new process_t, or NULL on failure.
 */
process_t *process_create_user(const char *name,
                                uint32_t user_eip, uint32_t user_esp,
                                uint32_t pd_phys);

/* Terminate the calling process (marks ZOMBIE and yields away) */
void process_exit(void);

/* Kill a process by PID.  Returns 0 on success, -1 if not found. */
int process_kill(uint32_t pid);

/* Block the calling process until process_wake() is called for it */
void process_block(void);

/* Make a BLOCKED process READY */
void process_wake(process_t *p);

/* Wake every BLOCKED process (called from keyboard IRQ on keypress) */
void process_wake_all_blocked(void);

/* Print a process table summary to the VGA console */
void process_print_all(void);

/*
 * sched_next – round-robin scheduler (called from process.asm only)
 */
process_t *sched_next(void);

/*
 * process_before_switch – called from process.asm before the stack swap.
 * Updates TSS.ESP0 and switches CR3 for the incoming process.
 */
void process_before_switch(process_t *next);

/*
 * process_yield – co-operative yield (also called by timer IRQ for preemption)
 * Implemented in kernel/process.asm.
 */
void process_yield(void);

/*
 * user_mode_enter – IRET trampoline for user-mode processes.
 * Declared here only so process.c can use its address.
 * Implemented in kernel/process.asm.
 */
void user_mode_enter(void);

#endif /* PROCESS_H */
