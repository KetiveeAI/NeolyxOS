.global gdt_load
.global idt_load
.global syscall_entry
.global context_switch
.global enter_user_mode
.global get_cpu_id
.global cpu_halt
.global enable_interrupts
.global disable_interrupts
.global read_cr0
.global write_cr0
.global read_cr3
.global write_cr3
.global read_cr4
.global write_cr4
.global invlpg
.global cli
.global sti
.global hlt
.global syscall_msr_init

.section .bss
.align 8
.global g_syscall_user_rsp
g_syscall_user_rsp:
    .quad 0
.section .text
.global syscall_enable

# Load GDT
gdt_load:
    lgdt (%rdi)
    # Reload segments
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    # Far jump to reload CS
    pushq $0x08
    leaq .reload_cs(%rip), %rax
    pushq %rax
    retfq
.reload_cs:
    ret

# Load IDT
idt_load:
    lidt (%rdi)
    ret

# System call entry point
# SYSCALL: RCX = user RIP, R11 = user RFLAGS, RAX = syscall number
# Args: RDI=arg1, RSI=arg2, RDX=arg3, R10=arg4, R8=arg5
#
# ABI guarantee to userspace: every register except RAX (return value),
# RCX and R11 (clobbered by SYSCALL/SYSRET) is preserved.
#
# FMASK clears IF on entry, so the global scratch slot below cannot be
# overwritten by an interrupt before it is consumed (single CPU).
# g_kernel_rsp0 always holds the top of the *current* process's kernel
# stack; the scheduler updates it on every switch.
syscall_entry:
    movq %rsp, g_syscall_user_rsp(%rip)     # Save user RSP without clobbering a GPR
    movq g_kernel_rsp0(%rip), %rsp          # Switch to this process's kernel stack

    pushq g_syscall_user_rsp(%rip)          # User RSP
    pushq %r11                              # User RFLAGS
    pushq %rcx                              # User RIP

    # Preserve every register userspace may rely on
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9
    # 15 qwords pushed onto a 16-byte aligned stack top: realign for the call
    subq $8, %rsp

    # syscall_handler(num, arg1, arg2, arg3, arg4, arg5)
    movq %r8, %r9                           # arg5
    movq %r10, %r8                          # arg4
    movq %rdx, %rcx                         # arg3
    movq %rsi, %rdx                         # arg2
    movq %rdi, %rsi                         # arg1
    movq %rax, %rdi                         # num
    call syscall_handler
    # RAX = return value (left untouched below)

    addq $8, %rsp
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    popq %rcx                               # User RIP
    popq %r11                               # User RFLAGS
    popq %rsp                               # User RSP (interrupts still masked)
    sysretq


# void sched_switch(uint64_t *save_rsp, uint64_t new_rsp)
#
# Kernel-stack switch used by the scheduler. Saves callee-saved registers on
# the current stack, stores RSP into *save_rsp, loads new_rsp and pops the
# same frame. A new process's stack is pre-built so the final RET lands in
# user_first_entry.
.global sched_switch
sched_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

# First instruction a newly created process runs (in kernel mode, on its
# own kernel stack, interrupts disabled). Never returns.
.global user_first_entry
user_first_entry:
    andq $-16, %rsp
    call process_first_run
1:  cli
    hlt
    jmp 1b

# First instruction a new kernel thread runs. R12 = entry, R13 = argument.
.global kthread_first_entry
kthread_first_entry:
    andq $-16, %rsp
    movq %r12, %rdi
    movq %r13, %rsi
    call kthread_start
1:  cli
    hlt
    jmp 1b

# void enter_user_mode_args(uint64_t stack, uint64_t entry,
#                           uint64_t arg0, uint64_t arg1)
# Like enter_user_mode, but starts the program with RDI = arg0 (argc) and
# RSI = arg1 (argv). All other registers are cleared.
.global enter_user_mode_args
enter_user_mode_args:
    cli
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushq $0x23             # SS
    pushq %rdi              # RSP
    pushq $0x202            # RFLAGS (IF=1)
    pushq $0x2B             # CS (64-bit user code)
    pushq %rsi              # RIP

    movq %rdx, %rdi         # argc
    movq %rcx, %rsi         # argv
    xor %rax, %rax
    xor %rbx, %rbx
    xor %rcx, %rcx
    xor %rdx, %rdx
    xor %rbp, %rbp
    xor %r8, %r8
    xor %r9, %r9
    xor %r10, %r10
    xor %r11, %r11
    xor %r12, %r12
    xor %r13, %r13
    xor %r14, %r14
    xor %r15, %r15
    iretq

# Context switch between processes
context_switch:
    # Save current context
    pushq %rbp
    movq %rsp, %rbp
    
    # Save callee-saved registers
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    # Save current stack pointer
    movq %rsp, (%rdi)
    
    # Load new stack pointer
    movq (%rsi), %rsp
    
    # Restore callee-saved registers
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    
    # Restore frame pointer
    popq %rbp
    ret

# Enter user mode (Ring 3)
# RDI = user stack pointer
# RSI = user entry point (RIP)
#
# IRET frame on stack (top to bottom): RIP, CS, RFLAGS, RSP, SS
# We push in reverse order so RIP is on top
enter_user_mode:
    # Disable interrupts during transition
    cli
    
    # Set up user data segments BEFORE building IRET frame
    mov $0x23, %ax          # User data segment (0x20 | DPL3)
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    # Build IRET frame on current kernel stack
    # Order: SS, RSP, RFLAGS, CS, RIP (pushed bottom to top)
    
    pushq $0x23             # SS - User data segment
    pushq %rdi              # RSP - User stack pointer
    pushq $0x202            # RFLAGS - IF enabled, reserved bit 1 set
    pushq $0x2B             # CS - User code 64-bit segment (0x28 | DPL3)
    pushq %rsi              # RIP - User entry point
    
    # Clear all general purpose registers for security
    # (prevents kernel data leakage to user space)
    xor %rax, %rax
    xor %rbx, %rbx
    xor %rcx, %rcx
    xor %rdx, %rdx
    xor %rdi, %rdi
    xor %rsi, %rsi
    xor %rbp, %rbp
    xor %r8, %r8
    xor %r9, %r9
    xor %r10, %r10
    xor %r11, %r11
    xor %r12, %r12
    xor %r13, %r13
    xor %r14, %r14
    xor %r15, %r15
    
    # Return to user mode - CPU will:
    # - Pop RIP, CS, RFLAGS, RSP, SS from stack
    # - Switch to Ring 3 based on CS DPL
    # - Enable interrupts (RFLAGS.IF was set)
    iretq

# Get CPU ID
get_cpu_id:
    movq $0, %rax
    cpuid
    ret

# CPU halt
cpu_halt:
    hlt
    ret

# Enable interrupts
enable_interrupts:
    sti
    ret

# Disable interrupts
disable_interrupts:
    cli
    ret

# Read CR0
read_cr0:
    movq %cr0, %rax
    ret

# Write CR0
write_cr0:
    movq %rdi, %cr0
    ret

# Read CR3
read_cr3:
    movq %cr3, %rax
    ret

# Write CR3
write_cr3:
    movq %rdi, %cr3
    ret

# Read CR4
read_cr4:
    movq %cr4, %rax
    ret

# Write CR4
write_cr4:
    movq %rdi, %cr4
    ret

# Invalidate page
invlpg:
    invlpg (%rdi)
    ret

# Disable interrupts
cli:
    cli
    ret

# Enable interrupts
sti:
    sti
    ret

# Halt CPU
hlt:
    hlt
    ret

# ============ SYSCALL MSR Setup ============
# void syscall_msr_init(void)
#
# Set up MSRs for SYSCALL/SYSRET instruction.
# This is CRITICAL - without this, SYSCALL will crash!
syscall_msr_init:
    # IA32_STAR (0xC0000081)
    # Bits 32-47: SYSCALL CS and SS (kernel segments)
    # Bits 48-63: SYSRET CS and SS (user segments)
    # Kernel: CS = 0x08, SS = 0x10
    # User: CS = 0x1B (0x18 | 3), SS = 0x23 (0x20 | 3)
    mov $0xC0000081, %ecx
    xor %eax, %eax              # Low 32 bits = 0
    mov $0x00180008, %edx       # High 32: user_cs_base=0x18, kernel_cs=0x08
    wrmsr
    
    # IA32_LSTAR (0xC0000082) = SYSCALL entry point
    mov $0xC0000082, %ecx
    lea syscall_entry(%rip), %rax
    mov %rax, %rdx
    shr $32, %rdx
    wrmsr
    
    # IA32_FMASK (0xC0000084) = RFLAGS mask
    # Clear IF, TF, DF on syscall entry (CRITICAL for interrupt safety)
    mov $0xC0000084, %ecx
    mov $0x0700, %eax           # Mask IF (0x200), TF (0x100), DF (0x400)
    xor %edx, %edx
    wrmsr
    
    ret

# void syscall_enable(void)
#
# Enable SYSCALL instruction by setting SCE bit in IA32_EFER.
syscall_enable:
    mov $0xC0000080, %ecx       # IA32_EFER
    rdmsr
    or $1, %eax                 # SCE = bit 0
    wrmsr
    ret