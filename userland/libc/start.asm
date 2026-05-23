; =============================================================================
; Vesper OS – User-space CRT0 (C runtime startup)
;
; This is the entry point for all user-mode ELF binaries.  The kernel sets
; EIP to _start when spawning the process.
;
; Responsibilities:
;   1. Call main() (defined by the user program)
;   2. Call exit() syscall when main() returns
; =============================================================================

[bits 32]

section .note.GNU-stack noalloc noexec nowrite progbits

section .text

global _start
extern main

_start:
    ; Call user's main function
    call main

    ; main() returned – call SYS_EXIT (syscall 0)
    mov eax, 0          ; SYS_EXIT
    int 0x80

    ; Should never reach here
.hang:
    jmp .hang
