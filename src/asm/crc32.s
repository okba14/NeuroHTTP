; crc32.s - Optimized CRC32 calculation in assembly language
; Based on x86-64 architecture with SSE4.2 support

global crc32_asm

; Function: crc32_asm
; Inputs:
;   rdi - pointer to data
;   rsi - length of data
; Outputs:
;   eax - CRC32 value

section .text

crc32_asm:
    ; Save registers we will use
    push rbx
    push rcx
    push rdx
    
    ; rdi = data
    ; rsi = length
    
    ; Check for SSE4.2 support
    mov rax, 1
    cpuid
    mov rcx, rbx
    shr rcx, 20  ; Check SSE4.2 bit
    test rcx, 1
    jz .no_sse42
    
    ; Use SSE4.2 CRC32 instructions
    mov rdx, rdi
    mov rcx, rsi
    xor eax, eax  ; Initialize CRC to 0
    
    ; Process 8 bytes at a time
    mov rbx, rcx
    shr rbx, 3  ; length / 8
    jz .process_bytes
    
.crc_loop:
    crc32 rax, qword [rdx]  ; Fixed: specify qword size
    
    add rdx, 8
    
    dec rbx
    jnz .crc_loop
    
    ; Process remaining bytes
    and rcx, 7  ; length % 8
    jz .done_sse
    
.process_bytes:
    mov bl, byte [rdx]  ; Fixed: specify byte size
    crc32 eax, ebx
    
    inc rdx
    
    dec rcx
    jnz .process_bytes
    
.done_sse:
    ; Invert the result (to match standard CRC32 implementation)
    not eax
    
    jmp .done
    
.no_sse42:
    ; Software CRC32 implementation (simplified)
    mov rdx, rdi
    mov rcx, rsi
    mov eax, 0xFFFFFFFF  ; Initialize CRC to 0xFFFFFFFF
    
.software_loop:
    mov bl, byte [rdx]  ; Fixed: specify byte size
    xor eax, ebx
    
    ; 8 iterations per byte
    mov edx, 8
.software_inner:
    shr eax, 1
    jnc .no_xor
    xor eax, 0xEDB88320
.no_xor:
    dec edx
    jnz .software_inner
    
    inc rdx
    
    dec rcx
    jnz .software_loop
    
    ; Invert the result
    not eax
    
.done:
    ; Restore registers
    pop rdx
    pop rcx
    pop rbx
    
    ret

segment .note.GNU-stack noexec
