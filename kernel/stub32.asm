[BITS 32]

global _start
extern kernel_main

_start:
    cli
    cld
    mov esp, 0x7FF00
    mov ebp, 0x7FF00

    lgdt [gdt_descriptor]

    ; Enable PAE in CR4
    mov eax, cr4
    or eax, (1 << 5)        ; CR4.PAE = 1
    mov cr4, eax

    ; Set up minimal page tables at 0x9000
    ; Build identity mapping for first 4 GiB using 2 MiB pages

    ; Clear the page table area (approx 2 pages)
    mov edi, 0x9000
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; PML4 @ 0x9000: single entry pointing to PDPT @ 0xA000
    mov dword [0x9000], 0xA003
    mov dword [0x9004], 0

    ; PDPT @ 0xA000: 4 entries, each pointing to PD
    mov dword [0xA000], 0xB003
    mov dword [0xA004], 0
    mov dword [0xA008], 0xC003
    mov dword [0xA00C], 0
    mov dword [0xA010], 0xD003
    mov dword [0xA014], 0
    mov dword [0xA018], 0xE003
    mov dword [0xA01C], 0

    ; PD @ 0xB000..0xE000: 512 entries each = 2048 total = 4 GiB with 2 MiB pages
    ; Fill each PD with 512 2 MiB page descriptors

    mov eax, 0x83            ; 2 MiB page, present, rw
    mov ecx, 0xB000          ; start at PD 0
    mov edi, 0               ; page counter

.fill_pd:
    mov [ecx], eax
    mov [ecx+4], edi
    add eax, 0x200000        ; next 2 MiB
    add ecx, 8
    cmp ecx, 0xF000          ; end of last PD
    jl .fill_pd

    ; Load PML4 address into CR3
    mov eax, 0x9000
    mov cr3, eax

    ; Set EFER.LME (long mode enable bit 8)
    mov ecx, 0xC0000080
    mov eax, 0x100
    xor edx, edx
    wrmsr

    ; Enable paging (CR0.PG = 1)
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ; Far jump to 64-bit code
    jmp 0x08:long_mode_start

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[BITS 64]
long_mode_start:
    ; Now in 64-bit mode; update segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, 0x7FF00
    mov rbp, 0x7FF00

    ; Call 64-bit kernel main
    call kernel_main

    ; Halt
    cli
.halt:
    hlt
    jmp .halt
