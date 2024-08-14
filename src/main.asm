org 0x7C00
bits 16

%define ENDL 0x0D, 0x0A

start:
    jmp main

puts:
    ; Save registers we will modify
    push si
    push ax

.loop:
    lodsb            ; Load next char in al
    or al, al
    jz .done
    mov ah, 0x0E     ; BIOS teletype output
    mov bh, 0
    int 0x10         ; BIOS interrupt to print character
    jmp .loop

.done:
    pop ax
    pop si
    ret

main:
    ; Setup data segments
    xor ax, ax       ; Clear ax (equivalent to mov ax, 0)
    mov ds, ax
    mov es, ax

    ; Setup stack
    mov ss, ax
    mov sp, 0x7C00   ; Stack grows downwards

    ; Print message
    mov si, msg_hello
    call puts

.halt:
    hlt
    jmp .halt

msg_hello: db 'Hello World!', ENDL, 0

times 510-($-$$) db 0
dw 0xAA55

