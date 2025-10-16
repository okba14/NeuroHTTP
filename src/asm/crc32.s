; crc32.s - Optimized CRC32 calculation using SSE4.2 and AVX2
; Based on x86-64 architecture with advanced instruction sets

global crc32_asm
global crc32_asm_avx2

section .text

; Standard CRC32 implementation using SSE4.2
crc32_asm:
    push rbx
    push rcx
    push rdx
    
    ; Check for SSE4.2 support
    push rbx
    mov eax, 1
    cpuid
    pop rbx
    test ecx, 1 << 20  ; Check SSE4.2 bit
    jz software_crc32
    
    mov rdx, rdi
    mov rcx, rsi
    xor eax, eax  ; Initialize CRC to 0
    
    ; Process 64 bytes at a time
    mov rbx, rcx
    shr rbx, 6  ; length / 64
    jz crc_process_32_bytes
    
crc_loop_64:
    crc32 eax, dword [rdx]
    crc32 eax, dword [rdx + 4]
    crc32 eax, dword [rdx + 8]
    crc32 eax, dword [rdx + 12]
    crc32 eax, dword [rdx + 16]
    crc32 eax, dword [rdx + 20]
    crc32 eax, dword [rdx + 24]
    crc32 eax, dword [rdx + 28]
    crc32 eax, dword [rdx + 32]
    crc32 eax, dword [rdx + 36]
    crc32 eax, dword [rdx + 40]
    crc32 eax, dword [rdx + 44]
    crc32 eax, dword [rdx + 48]
    crc32 eax, dword [rdx + 52]
    crc32 eax, dword [rdx + 56]
    crc32 eax, dword [rdx + 60]
    
    add rdx, 64
    dec rbx
    jnz crc_loop_64
    
    ; Process 32 bytes
crc_process_32_bytes:
    mov rbx, rcx
    shr rbx, 5  ; length / 32
    and rbx, 1
    jz crc_process_16_bytes
    
crc_loop_32:
    crc32 eax, dword [rdx]
    crc32 eax, dword [rdx + 4]
    crc32 eax, dword [rdx + 8]
    crc32 eax, dword [rdx + 12]
    crc32 eax, dword [rdx + 16]
    crc32 eax, dword [rdx + 20]
    crc32 eax, dword [rdx + 24]
    crc32 eax, dword [rdx + 28]
    
    add rdx, 32
    
crc_process_16_bytes:
    ; Process 16 bytes
    mov rbx, rcx
    shr rbx, 4  ; length / 16
    and rbx, 1
    jz crc_process_8_bytes
    
crc_loop_16:
    crc32 eax, dword [rdx]
    crc32 eax, dword [rdx + 4]
    crc32 eax, dword [rdx + 8]
    crc32 eax, dword [rdx + 12]
    
    add rdx, 16
    
crc_process_8_bytes:
    ; Process 8 bytes
    mov rbx, rcx
    shr rbx, 3  ; length / 8
    and rbx, 1
    jz crc_process_bytes
    
crc_loop_8:
    crc32 eax, dword [rdx]
    crc32 eax, dword [rdx + 4]
    add rdx, 8
    
crc_process_bytes:
    ; Process remaining bytes (0-7 bytes)
    and rcx, 7
    jz crc_done_sse
    
crc_process_bytes_loop:
    mov bl, byte [rdx]
    crc32 eax, ebx
    
    inc rdx
    dec rcx
    jnz crc_process_bytes_loop
    
crc_done_sse:
    not eax
    jmp crc_done
    
software_crc32:
    mov rdx, rdi
    mov rcx, rsi
    mov eax, 0xFFFFFFFF
    
    ; Process 8 bytes at a time
    mov rbx, rcx
    shr rbx, 3
    jz software_bytes
    
software_loop_8:
    mov r8, qword [rdx]
    
    ; Process each byte with optimized CRC32 algorithm
    mov r9, r8
    xor eax, r9d
    shr r9, 8
    xor eax, r9d
    shr r9, 8
    xor eax, r9d
    shr r9, 8
    xor eax, r9d
    shr r9, 8
    xor eax, r9d
    shr r9, 8
    xor eax, r9d
    shr r9, 8
    xor eax, r9d
    
    add rdx, 8
    dec rbx
    jnz software_loop_8
    
software_bytes:
    ; Process remaining bytes (0-7 bytes)
    and rcx, 7
    jz software_done
    
software_bytes_loop:
    mov bl, byte [rdx]
    xor eax, ebx
    
    mov r8d, 8
software_inner:
    shr eax, 1
    jnc software_no_xor
    xor eax, 0xEDB88320
software_no_xor:
    dec r8d
    jnz software_inner
    
    inc rdx
    dec rcx
    jnz software_bytes_loop
    
software_done:
    not eax
    
crc_done:
    pop rdx
    pop rcx
    pop rbx
    ret

; AVX2 optimized CRC32 implementation
crc32_asm_avx2:
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi
    
    ; Check for AVX2 support
    push rbx
    mov eax, 7
    xor ecx, ecx
    cpuid
    pop rbx
    test ebx, 1 << 5
    jz crc32_asm
    
    mov rdx, rdi
    mov rcx, rsi
    xor eax, eax
    
    ; Process 128 bytes at a time using AVX2
    mov rbx, rcx
    shr rbx, 7  ; length / 128
    jz avx_process_32_bytes
    
avx_loop_128:
    vmovdqu ymm0, [rdx]
    vmovdqu ymm1, [rdx + 32]
    vmovdqu ymm2, [rdx + 64]
    vmovdqu ymm3, [rdx + 96]
    
    ; Extract and process each 32-bit chunk
    vextracti128 xmm0, ymm0, 0
    vextracti128 xmm1, ymm1, 0
    vextracti128 xmm2, ymm2, 0
    vextracti128 xmm3, ymm3, 0
    
    ; Process first 128-bit register (4 dwords)
    movd ebx, xmm0
    crc32 eax, ebx
    pextrd ebx, xmm0, 1
    crc32 eax, ebx
    pextrd ebx, xmm0, 2
    crc32 eax, ebx
    pextrd ebx, xmm0, 3
    crc32 eax, ebx
    
    ; Process second 128-bit register
    movd ebx, xmm1
    crc32 eax, ebx
    pextrd ebx, xmm1, 1
    crc32 eax, ebx
    pextrd ebx, xmm1, 2
    crc32 eax, ebx
    pextrd ebx, xmm1, 3
    crc32 eax, ebx
    
    ; Process third 128-bit register
    movd ebx, xmm2
    crc32 eax, ebx
    pextrd ebx, xmm2, 1
    crc32 eax, ebx
    pextrd ebx, xmm2, 2
    crc32 eax, ebx
    pextrd ebx, xmm2, 3
    crc32 eax, ebx
    
    ; Process fourth 128-bit register
    movd ebx, xmm3
    crc32 eax, ebx
    pextrd ebx, xmm3, 1
    crc32 eax, ebx
    pextrd ebx, xmm3, 2
    crc32 eax, ebx
    pextrd ebx, xmm3, 3
    crc32 eax, ebx
    
    add rdx, 128
    dec rbx
    jnz avx_loop_128
    
    ; Process 64 bytes
avx_process_32_bytes:
    mov rbx, rcx
    shr rbx, 6
    and rbx, 1
    jz avx_process_16_bytes
    
avx_loop_64:
    vmovdqu ymm0, [rdx]
    vmovdqu ymm1, [rdx + 32]
    
    ; Extract and process each 32-bit chunk
    vextracti128 xmm0, ymm0, 0
    vextracti128 xmm1, ymm1, 0
    
    ; Process first 128-bit register
    movd ebx, xmm0
    crc32 eax, ebx
    pextrd ebx, xmm0, 1
    crc32 eax, ebx
    pextrd ebx, xmm0, 2
    crc32 eax, ebx
    pextrd ebx, xmm0, 3
    crc32 eax, ebx
    
    ; Process second 128-bit register
    movd ebx, xmm1
    crc32 eax, ebx
    pextrd ebx, xmm1, 1
    crc32 eax, ebx
    pextrd ebx, xmm1, 2
    crc32 eax, ebx
    pextrd ebx, xmm1, 3
    crc32 eax, ebx
    
    add rdx, 64
    
avx_process_16_bytes:
    ; Process 32 bytes
    mov rbx, rcx
    shr rbx, 5
    and rbx, 1
    jz avx_process_bytes
    
avx_loop_32:
    vmovdqu ymm0, [rdx]
    vextracti128 xmm0, ymm0, 0
    
    ; Process 128-bit register
    movd ebx, xmm0
    crc32 eax, ebx
    pextrd ebx, xmm0, 1
    crc32 eax, ebx
    pextrd ebx, xmm0, 2
    crc32 eax, ebx
    pextrd ebx, xmm0, 3
    crc32 eax, ebx
    
    add rdx, 32
    
avx_process_bytes:
    ; Process 16 bytes
    mov rbx, rcx
    shr rbx, 4
    and rbx, 1
    jz avx_bytes_loop
    
avx_loop_16:
    movdqu xmm0, [rdx]
    
    ; Process 128-bit register
    movd ebx, xmm0
    crc32 eax, ebx
    pextrd ebx, xmm0, 1
    crc32 eax, ebx
    pextrd ebx, xmm0, 2
    crc32 eax, ebx
    pextrd ebx, xmm0, 3
    crc32 eax, ebx
    
    add rdx, 16
    
avx_bytes_loop:
    ; Process remaining bytes (0-15 bytes)
    and rcx, 15
    jz avx_done
    
    mov bl, byte [rdx]
    crc32 eax, ebx
    
    inc rdx
    dec rcx
    jnz avx_bytes_loop
    
avx_done:
    not eax
    
    pop rsi
    pop rdi
    pop rdx
    pop rcx
    pop rbx
    ret
