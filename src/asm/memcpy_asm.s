; memcpy_asm.s - Optimized memory copy in assembly language
; Based on x86-64 architecture

global memcpy_asm

; Function: memcpy_asm
; Inputs:
;   rdi - memory destination
;   rsi - memory source
;   rdx - number of bytes to copy
; Outputs:
;   rdi - pointer to memory destination

section .text

memcpy_asm:
    ; Save registers we will use
    push rbx
    push rcx
    push rsi
    
    ; rdi = dest
    ; rsi = src
    ; rdx = count
    
    ; Check for zero count
    test rdx, rdx
    jz .done
    
    ; Copy one byte at a time for small counts
    cmp rdx, 16
    jb .copy_byte
    
    ; Check memory alignment
    test rdi, 15
    jnz .copy_unaligned
    
    ; Copy 16 bytes at a time (aligned)
    mov rcx, rdx
    shr rcx, 4  ; count / 16
    jz .copy_remaining
    
.copy_loop:
    movdqu xmm0, [rsi]
    movdqu [rdi], xmm0
    
    add rsi, 16
    add rdi, 16
    
    dec rcx
    jnz .copy_loop
    
    ; Copy remaining bytes
    and rdx, 15  ; count % 16
    jz .done
    
.copy_remaining:
    mov rcx, rdx
.copy_remaining_loop:
    mov al, [rsi]
    mov [rdi], al
    
    inc rsi
    inc rdi
    
    dec rcx
    jnz .copy_remaining_loop
    
    jmp .done
    
.copy_unaligned:
    ; Copy 8 bytes at a time (unaligned)
    mov rcx, rdx
    shr rcx, 3  ; count / 8
    jz .copy_byte_remaining
    
.copy_unaligned_loop:
    mov rax, [rsi]
    mov [rdi], rax
    
    add rsi, 8
    add rdi, 8
    
    dec rcx
    jnz .copy_unaligned_loop
    
    ; Copy remaining bytes
    and rdx, 7  ; count % 8
    jz .done
    
.copy_byte_remaining:
    mov rcx, rdx
.copy_byte_loop:
    mov al, [rsi]
    mov [rdi], al
    
    inc rsi
    inc rdi
    
    dec rcx
    jnz .copy_byte_loop
    
    jmp .done
    
.copy_byte:
    ; Copy one byte at a time
    mov rcx, rdx
.copy_byte_only:
    mov al, [rsi]
    mov [rdi], al
    
    inc rsi
    inc rdi
    
    dec rcx
    jnz .copy_byte_only
    
.done:
    ; Restore registers
    pop rsi
    pop rcx
    pop rbx
    
    ; Return destination pointer
    mov rax, rdi
    ret
