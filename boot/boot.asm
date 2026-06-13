[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov ax, 0x0003
    int 0x10

    mov si, msg_load
    call print16

    mov ah, 0x02
    mov al, 32
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, 0x00
    mov bx, 0x1000
    int 0x13
    jc disk_error

    mov si, msg_ok
    call print16

    ; Switch to a VBE linear framebuffer mode (800x600x8bpp)
    ; and stash the framebuffer info for the kernel.
    mov ax, 0x900
    mov es, ax
    xor di, di

    mov ax, 0x4F01
    mov cx, 0x0103
    int 0x10
    cmp ax, 0x004F
    jne disk_error

    mov ax, 0x4F02
    mov bx, 0x4103
    int 0x10
    cmp ax, 0x004F
    jne disk_error

    mov dword [0x8000], 0x31424651    ; 'QFB1'
    mov eax, [0x9028]                 ; PhysBasePtr
    mov dword [0x8004], eax
    mov ax, [0x9018]                  ; XResolution
    mov [0x8008], ax
    mov ax, [0x901A]                  ; YResolution
    mov [0x800A], ax
    mov ax, [0x9010]                  ; BytesPerScanLine
    mov [0x800C], ax
    xor ax, ax
    mov al, [0x9019]                  ; BitsPerPixel
    mov [0x800E], ax

    cli
    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp 0x08:init_pm

disk_error:
    mov si, msg_err
    call print16
    cli
    hlt

print16:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print16
.done:
    ret

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[BITS 32]
init_pm:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x7FF00

    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xA1, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al
    mov al, 0x01
    out 0x21, al
    out 0xA1, al
    mov al, 0xFD
    out 0x21, al
    mov al, 0xFF
    out 0xA1, al

    jmp 0x08:0x1000

msg_load  db 'QRTOS Bootloader - Loading...', 13, 10, 0
msg_ok    db 'Loaded! Entering protected mode.', 13, 10, 0
msg_err   db 'ERROR: Load failed!', 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
