#include "process.h"
#include "fd.h"
#include "paging.h"
#include "tss.h"
#include "string.h"
#include "vga.h"

#define NULL ((void *)0)

/* -------------------------------------------------------------------------
 * Module-level state
 * ---------------------------------------------------------------------- */
process_t  *current_process               = NULL;
static process_t  process_table[MAX_PROCESSES];
static uint32_t   next_pid = 0;

static void process_reap_zombies(void)
{
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &process_table[i];
        if (p == current_process || p->state != PROC_ZOMBIE) {
            continue;
        }

        fd_close_all_for_pid(p->pid);
        if (p->pd_phys) {
            paging_destroy_pd(p->pd_phys);
        }
        memset(p, 0, sizeof(*p));
        p->state = PROC_UNUSED;
    }
}

/* -------------------------------------------------------------------------
 * Idle process – runs when every other process is blocked or zombie.
 * ---------------------------------------------------------------------- */
static void idle_process_fn(void)
{
    while (1) {
        __asm__ volatile ("sti; hlt");
        process_yield();
    }
}

/* -------------------------------------------------------------------------
 * process_init – zero the table and create the idle process
 * ---------------------------------------------------------------------- */
void process_init(void)
{
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].state   = PROC_UNUSED;
        process_table[i].pid     = 0;
        process_table[i].pd_phys = 0;
        process_table[i].is_user = 0;
    }
    next_pid = 0;

    /* Create idle process first */
    process_create("idle", idle_process_fn);
}

/* -------------------------------------------------------------------------
 * Internal helper: find a free table slot
 * ---------------------------------------------------------------------- */
static process_t *alloc_slot(void)
{
    process_reap_zombies();

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            return &process_table[i];
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Internal helper: push the callee-save frame that process_yield expects
 *
 * Initial kernel-stack layout (from top downward):
 *   [esp+0]  return address  → entry function (or user_mode_enter)
 *   [esp+4]  EBX  (0)
 *   [esp+8]  ESI  (0)
 *   [esp+12] EDI  (0)
 *   [esp+16] EBP  (0)
 * ---------------------------------------------------------------------- */
static uint32_t build_initial_kstack(process_t *p, void (*ret_addr)(void))
{
    uint32_t *stk = (uint32_t *)(p->kstack + PROCESS_KSTACK_SIZE);
    *(--stk) = (uint32_t)ret_addr;
    *(--stk) = 0u;   /* EBX */
    *(--stk) = 0u;   /* ESI */
    *(--stk) = 0u;   /* EDI */
    *(--stk) = 0u;   /* EBP */
    return (uint32_t)stk;
}

/* -------------------------------------------------------------------------
 * process_create – allocate a kernel-mode thread
 * ---------------------------------------------------------------------- */
process_t *process_create(const char *name, void (*entry)(void))
{
    process_t *p = alloc_slot();
    if (!p) {
        return NULL;
    }

    p->pid     = ++next_pid;
    p->state   = PROC_READY;
    p->is_user = 0;
    p->pd_phys = 0u;   /* kernel threads use the shared kernel PD */
    p->user_eip = 0u;
    p->user_esp = 0u;
    strncpy(p->name, name, sizeof(p->name) - 1u);
    p->name[sizeof(p->name) - 1u] = '\0';

    p->esp = build_initial_kstack(p, entry);
    return p;
}

/* -------------------------------------------------------------------------
 * process_create_user – allocate a user-mode (ring-3) process
 * ---------------------------------------------------------------------- */
process_t *process_create_user(const char *name,
                                uint32_t user_eip, uint32_t user_esp,
                                uint32_t pd_phys)
{
    process_t *p = alloc_slot();
    if (!p) {
        return NULL;
    }

    p->pid      = ++next_pid;
    p->state    = PROC_READY;
    p->is_user  = 1;
    p->pd_phys  = pd_phys;
    p->user_eip = user_eip;
    p->user_esp = user_esp;
    strncpy(p->name, name, sizeof(p->name) - 1u);
    p->name[sizeof(p->name) - 1u] = '\0';

    /* Return address is the user_mode_enter trampoline (defined in process.asm) */
    p->esp = build_initial_kstack(p, user_mode_enter);
    return p;
}

/* -------------------------------------------------------------------------
 * process_before_switch – called from process.asm before the stack swap.
 * Updates TSS.ESP0 (for ring-3 → ring-0 transitions) and switches CR3 if
 * the incoming process has its own page directory.
 * ---------------------------------------------------------------------- */
void process_before_switch(process_t *next)
{
    /* Update the kernel-stack pointer the CPU will use on the next ring-0 entry */
    tss_set_kernel_stack((uint32_t)(next->kstack + PROCESS_KSTACK_SIZE));

    /* Switch page directory only when necessary (avoids a needless TLB flush) */
    uint32_t target_pd = next->pd_phys
                         ? next->pd_phys
                         : paging_get_kernel_pd_phys();

    if (target_pd != paging_get_cr3()) {
        paging_switch(target_pd);
    }
}

/* -------------------------------------------------------------------------
 * process_exit – terminate the calling process and yield away
 * ---------------------------------------------------------------------- */
void process_exit(void)
{
    if (current_process) {
        current_process->state = PROC_ZOMBIE;
    }
    process_yield();
    while (1) {}   /* should never be reached */
}

/* -------------------------------------------------------------------------
 * process_kill – terminate any process by PID
 * ---------------------------------------------------------------------- */
int process_kill(uint32_t pid)
{
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (i == 0u && process_table[i].pid == pid) {
            return PROCESS_KILL_ERR_FORBIDDEN;
        }
        if (process_table[i].pid == pid &&
            process_table[i].state != PROC_UNUSED &&
            process_table[i].state != PROC_ZOMBIE) {
            if (&process_table[i] == current_process) {
                process_exit();
            }
            process_table[i].state = PROC_ZOMBIE;
            return 0;
        }
    }
    return PROCESS_KILL_ERR_NOT_FOUND;
}

/* -------------------------------------------------------------------------
 * process_block – suspend the calling process until woken
 * ---------------------------------------------------------------------- */
void process_block(void)
{
    if (current_process) {
        current_process->state = PROC_BLOCKED;
    }
    process_yield();
}

/* -------------------------------------------------------------------------
 * process_wake / process_wake_all_blocked
 * ---------------------------------------------------------------------- */
void process_wake(process_t *p)
{
    if (p && p->state == PROC_BLOCKED) {
        p->state = PROC_READY;
    }
}

void process_wake_all_blocked(void)
{
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_BLOCKED) {
            process_table[i].state = PROC_READY;
        }
    }
}

/* -------------------------------------------------------------------------
 * sched_next – round-robin: find the next READY process
 * Called from process.asm; do not call directly.
 * ---------------------------------------------------------------------- */
process_t *sched_next(void)
{
    process_reap_zombies();

    if (!current_process) {
        /* Bootstrap: return the first ready process */
        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].state == PROC_READY ||
                process_table[i].state == PROC_RUNNING) {
                process_table[i].state = PROC_RUNNING;
                return &process_table[i];
            }
        }
        process_table[0].state = PROC_RUNNING;
        return &process_table[0];
    }

    uint32_t curr_idx = (uint32_t)(current_process - process_table);

    if (current_process->state == PROC_RUNNING) {
        current_process->state = PROC_READY;
    }

    for (uint32_t i = 1u; i <= MAX_PROCESSES; i++) {
        uint32_t idx = (curr_idx + i) % MAX_PROCESSES;
        if (process_table[idx].state == PROC_READY) {
            process_table[idx].state = PROC_RUNNING;
            return &process_table[idx];
        }
    }

    /* Fallback: idle process (index 0) */
    process_table[0].state = PROC_RUNNING;
    return &process_table[0];
}

/* -------------------------------------------------------------------------
 * process_print_all – 'ps' command helper
 * ---------------------------------------------------------------------- */
void process_print_all(void)
{
    static const char * const state_names[] = {
        "unused ", "ready  ", "running", "blocked", "zombie "
    };

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("PID  STATE    MODE    NAME\n");
    vga_puts("---  -------  ------  ----------------\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        const process_t *p = &process_table[i];
        if (p->state == PROC_UNUSED) {
            continue;
        }
        vga_print_uint(p->pid);
        vga_puts("    ");
        vga_puts(state_names[p->state]);
        vga_puts("  ");
        vga_puts(p->is_user ? "user  " : "kernel");
        vga_puts("  ");
        vga_puts(p->name);
        vga_putchar('\n');
    }
}
