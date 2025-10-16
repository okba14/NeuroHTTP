; memcpy_asm.s - Highly optimized memory copy with AVX-512 and non-temporal stores
; Based on x86-64 architecture with advanced instruction sets

global memcpy_asm
global memcpy_asm_avx2
global memcpy_asm_avx512

; Function: memcpy_asm (SSE2 optimized)
; Inputs:
;   rdi - memory destination
;   rsi - memory source
;   rdx - number of bytes to copy
; Outputs:
;   rdi - pointer to memory destination

; Function: memcpy_asm_avx2 (AVX2 optimized)
; Inputs:
;   rdi - memory destination
;   rsi - memory source
;   rdx - number of bytes to copy
; Outputs:
;   rdi - pointer to memory destination

; Function: memcpy_asm_avx512 (AVX-512 optimized)
; Inputs:
;   rdi - memory destination
;   rsi - memory source
;   rdx - number of bytes to copy
; Outputs:
;   rdi - pointer to memory destination

section .text

; Standard memcpy using SSE2
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
    
    ; Check for small counts
    cmp rdx, 128
    jb .copy_small
    
    ; Check memory alignment for optimal performance
    test rdi, 15
    jnz .copy_unaligned
    
    test rsi, 15
    jnz .copy_unaligned
    
    ; Aligned copy using SSE2 with non-temporal stores for large copies
    mov rcx, rdx
    shr rcx, 7  ; count / 128
    jz .copy_64_bytes
    
.copy_128_bytes_loop:
    ; Load 128 bytes using SSE2
    movdqa xmm0, [rsi]
    movdqa xmm1, [rsi + 16]
    movdqa xmm2, [rsi + 32]
    movdqa xmm3, [rsi + 48]
    movdqa xmm4, [rsi + 64]
    movdqa xmm5, [rsi + 80]
    movdqa xmm6, [rsi + 96]
    movdqa xmm7, [rsi + 112]
    
    ; Use non-temporal stores to avoid polluting the cache
    movntdq [rdi], xmm0
    movntdq [rdi + 16], xmm1
    movntdq [rdi + 32], xmm2
    movntdq [rdi + 48], xmm3
    movntdq [rdi + 64], xmm4
    movntdq [rdi + 80], xmm5
    movntdq [rdi + 96], xmm6
    movntdq [rdi + 112], xmm7
    
    add rsi, 128
    add rdi, 128
    
    dec rcx
    jnz .copy_128_bytes_loop
    
    ; Process remaining 64 bytes
    mov rcx, rdx
    shr rcx, 6  ; count / 64
    and rcx, 1  ; Check if we have 64 bytes remaining
    jz .copy_32_bytes
    
.copy_64_bytes:
    ; Load 64 bytes using SSE2
    movdqa xmm0, [rsi]
    movdqa xmm1, [rsi + 16]
    movdqa xmm2, [rsi + 32]
    movdqa xmm3, [rsi + 48]
    
    ; Use non-temporal stores
    movntdq [rdi], xmm0
    movntdq [rdi + 16], xmm1
    movntdq [rdi + 32], xmm2
    movntdq [rdi + 48], xmm3
    
    add rsi, 64
    add rdi, 64
    
.copy_32_bytes:
    ; Process remaining 32 bytes
    mov rcx, rdx
    shr rcx, 5  ; count / 32
    and rcx, 1  ; Check if we have 32 bytes remaining
    jz .copy_16_bytes
    
.copy_32_bytes_loop:
    ; Load 32 bytes using SSE2
    movdqa xmm0, [rsi]
    movdqa xmm1, [rsi + 16]
    
    ; Use non-temporal stores
    movntdq [rdi], xmm0
    movntdq [rdi + 16], xmm1
    
    add rsi, 32
    add rdi, 32
    
.copy_16_bytes:
    ; Process remaining 16 bytes
    mov rcx, rdx
    shr rcx, 4  ; count / 16
    and rcx, 1  ; Check if we have 16 bytes remaining
    jz .copy_8_bytes
    
.copy_16_bytes_loop:
    ; Load 16 bytes using SSE2
    movdqa xmm0, [rsi]
    
    ; Use non-temporal stores
    movntdq [rdi], xmm0
    
    add rsi, 16
    add rdi, 16
    
.copy_8_bytes:
    ; Process remaining 8 bytes
    mov rcx, rdx
    shr rcx, 3  ; count / 8
    and rcx, 1  ; Check if we have 8 bytes remaining
    jz .copy_4_bytes
    
.copy_8_bytes_loop:
    ; Load 8 bytes
    mov rax, [rsi]
    
    ; Store 8 bytes
    mov [rdi], rax
    
    add rsi, 8
    add rdi, 8
    
.copy_4_bytes:
    ; Process remaining 4 bytes
    mov rcx, rdx
    shr rcx, 2  ; count / 4
    and rcx, 1  ; Check if we have 4 bytes remaining
    jz .copy_2_bytes
    
.copy_4_bytes_loop:
    ; Load 4 bytes
    mov eax, [rsi]
    
    ; Store 4 bytes
    mov [rdi], eax
    
    add rsi, 4
    add rdi, 4
    
.copy_2_bytes:
    ; Process remaining 2 bytes
    mov rcx, rdx
    shr rcx, 1  ; count / 2
    and rcx, 1  ; Check if we have 2 bytes remaining
    jz .copy_1_byte
    
.copy_2_bytes_loop:
    ; Load 2 bytes
    mov ax, [rsi]
    
    ; Store 2 bytes
    mov [rdi], ax
    
    add rsi, 2
    add rdi, 2
    
.copy_1_byte:
    ; Process remaining byte
    and rdx, 1  ; count % 2
    jz .copy_done
    
    ; Load 1 byte
    mov al, [rsi]
    
    ; Store 1 byte
    mov [rdi], al
    
.copy_done:
    ; SFENCE to ensure non-temporal stores are visible
    sfence
    
    jmp .done
    
.copy_unaligned:
    ; Unaligned copy using SSE2
    mov rcx, rdx
    shr rcx, 4  ; count / 16
    jz .copy_unaligned_8_bytes
    
.copy_unaligned_loop:
    ; Load 16 bytes using unaligned SSE2
    movdqu xmm0, [rsi]
    
    ; Store 16 bytes using unaligned SSE2
    movdqu [rdi], xmm0
    
    add rsi, 16
    add rdi, 16
    
    dec rcx
    jnz .copy_unaligned_loop
    
.copy_unaligned_8_bytes:
    ; Process remaining 8 bytes
    mov rcx, rdx
    shr rcx, 3  ; count / 8
    and rcx, 1  ; Check if we have 8 bytes remaining
    jz .copy_unaligned_4_bytes
    
.copy_unaligned_8_bytes_loop:
    ; Load 8 bytes
    mov rax, [rsi]
    
    ; Store 8 bytes
    mov [rdi], rax
    
    add rsi, 8
    add rdi, 8
    
.copy_unaligned_4_bytes:
    ; Process remaining 4 bytes
    mov rcx, rdx
    shr rcx, 2  ; count / 4
    and rcx, 1  ; Check if we have 4 bytes remaining
    jz .copy_unaligned_2_bytes
    
.copy_unaligned_4_bytes_loop:
    ; Load 4 bytes
    mov eax, [rsi]
    
    ; Store 4 bytes
    mov [rdi], eax
    
    add rsi, 4
    add rdi, 4
    
.copy_unaligned_2_bytes:
    ; Process remaining 2 bytes
    mov rcx, rdx
    shr rcx, 1  ; count / 2
    and rcx, 1  ; Check if we have 2 bytes remaining
    jz .copy_unaligned_1_byte
    
.copy_unaligned_2_bytes_loop:
    ; Load 2 bytes
    mov ax, [rsi]
    
    ; Store 2 bytes
    mov [rdi], ax
    
    add rsi, 2
    add rdi, 2
    
.copy_unaligned_1_byte:
    ; Process remaining byte
    and rdx, 1  ; count % 2
    jz .copy_done
    
    ; Load 1 byte
    mov al, [rsi]
    
    ; Store 1 byte
    mov [rdi], al
    
    jmp .copy_done
    
.copy_small:
    ; Copy small blocks using optimized byte-by-byte copy
    mov rcx, rdx
    rep movsb
    
    jmp .done
    
.done:
    ; Restore registers
    pop rsi
    pop rcx
    pop rbx
    
    ; Return destination pointer
    mov rax, rdi
    ret

; Advanced memcpy using AVX2
memcpy_asm_avx2:
    ; Save registers we will use
    push rbx
    push rcx
    push rsi
    
    ; rdi = dest
    ; rsi = src
    ; rdx = count
    
    ; Check for zero count
    test rdx, rdx
    jz .avx2_done
    
    ; Check for AVX2 support
    push rbx
    mov eax, 7
    xor ecx, ecx
    cpuid
    pop rbx
    test ebx, 1 << 5  ; Check AVX2 bit
    jz memcpy_asm  ; Fall back to SSE2 implementation
    
    ; Check for small counts
    cmp rdx, 256
    jb .avx2_copy_small
    
    ; Check memory alignment for optimal performance
    test rdi, 31
    jnz .avx2_copy_unaligned
    
    test rsi, 31
    jnz .avx2_copy_unaligned
    
    ; Aligned copy using AVX2 with non-temporal stores for large copies
    mov rcx, rdx
    shr rcx, 8  ; count / 256
    jz .avx2_copy_128_bytes
    
.avx2_copy_256_bytes_loop:
    ; Load 256 bytes using AVX2
    vmovdqa ymm0, [rsi]
    vmovdqa ymm1, [rsi + 32]
    vmovdqa ymm2, [rsi + 64]
    vmovdqa ymm3, [rsi + 96]
    vmovdqa ymm4, [rsi + 128]
    vmovdqa ymm5, [rsi + 160]
    vmovdqa ymm6, [rsi + 192]
    vmovdqa ymm7, [rsi + 224]
    
    ; Use non-temporal stores to avoid polluting the cache
    vmovntdq [rdi], ymm0
    vmovntdq [rdi + 32], ymm1
    vmovntdq [rdi + 64], ymm2
    vmovntdq [rdi + 96], ymm3
    vmovntdq [rdi + 128], ymm4
    vmovntdq [rdi + 160], ymm5
    vmovntdq [rdi + 192], ymm6
    vmovntdq [rdi + 224], ymm7
    
    add rsi, 256
    add rdi, 256
    
    dec rcx
    jnz .avx2_copy_256_bytes_loop
    
    ; Process remaining 128 bytes
    mov rcx, rdx
    shr rcx, 7  ; count / 128
    and rcx, 1  ; Check if we have 128 bytes remaining
    jz .avx2_copy_64_bytes
    
.avx2_copy_128_bytes:
    ; Load 128 bytes using AVX2
    vmovdqa ymm0, [rsi]
    vmovdqa ymm1, [rsi + 32]
    vmovdqa ymm2, [rsi + 64]
    vmovdqa ymm3, [rsi + 96]
    
    ; Use non-temporal stores
    vmovntdq [rdi], ymm0
    vmovntdq [rdi + 32], ymm1
    vmovntdq [rdi + 64], ymm2
    vmovntdq [rdi + 96], ymm3
    
    add rsi, 128
    add rdi, 128
    
.avx2_copy_64_bytes:
    ; Process remaining 64 bytes
    mov rcx, rdx
    shr rcx, 6  ; count / 64
    and rcx, 1  ; Check if we have 64 bytes remaining
    jz .avx2_copy_32_bytes
    
.avx2_copy_64_bytes_loop:
    ; Load 64 bytes using AVX2
    vmovdqa ymm0, [rsi]
    vmovdqa ymm1, [rsi + 32]
    
    ; Use non-temporal stores
    vmovntdq [rdi], ymm0
    vmovntdq [rdi + 32], ymm1
    
    add rsi, 64
    add rdi, 64
    
.avx2_copy_32_bytes:
    ; Process remaining 32 bytes
    mov rcx, rdx
    shr rcx, 5  ; count / 32
    and rcx, 1  ; Check if we have 32 bytes remaining
    jz .avx2_copy_16_bytes
    
.avx2_copy_32_bytes_loop:
    ; Load 32 bytes using AVX2
    vmovdqa ymm0, [rsi]
    
    ; Use non-temporal stores
    vmovntdq [rdi], ymm0
    
    add rsi, 32
    add rdi, 32
    
.avx2_copy_16_bytes:
    ; Process remaining 16 bytes
    mov rcx, rdx
    shr rcx, 4  ; count / 16
    and rcx, 1  ; Check if we have 16 bytes remaining
    jz .avx2_copy_8_bytes
    
.avx2_copy_16_bytes_loop:
    ; Load 16 bytes using SSE
    movdqa xmm0, [rsi]
    
    ; Use non-temporal stores
    movntdq [rdi], xmm0
    
    add rsi, 16
    add rdi, 16
    
.avx2_copy_8_bytes:
    ; Process remaining 8 bytes
    mov rcx, rdx
    shr rcx, 3  ; count / 8
    and rcx, 1  ; Check if we have 8 bytes remaining
    jz .avx2_copy_4_bytes
    
.avx2_copy_8_bytes_loop:
    ; Load 8 bytes
    mov rax, [rsi]
    
    ; Store 8 bytes
    mov [rdi], rax
    
    add rsi, 8
    add rdi, 8
    
.avx2_copy_4_bytes:
    ; Process remaining 4 bytes
    mov rcx, rdx
    shr rcx, 2  ; count / 4
    and rcx, 1  ; Check if we have 4 bytes remaining
    jz .avx2_copy_2_bytes
    
.avx2_copy_4_bytes_loop:
    ; Load 4 bytes
    mov eax, [rsi]
    
    ; Store 4 bytes
    mov [rdi], eax
    
    add rsi, 4
    add rdi, 4
    
.avx2_copy_2_bytes:
    ; Process remaining 2 bytes
    mov rcx, rdx
    shr rcx, 1  ; count / 2
    and rcx, 1  ; Check if we have 2 bytes remaining
    jz .avx2_copy_1_byte
    
.avx2_copy_2_bytes_loop:
    ; Load 2 bytes
    mov ax, [rsi]
    
    ; Store 2 bytes
    mov [rdi], ax
    
    add rsi, 2
    add rdi, 2
    
.avx2_copy_1_byte:
    ; Process remaining byte
    and rdx, 1  ; count % 2
    jz .avx2_copy_done
    
    ; Load 1 byte
    mov al, [rsi]
    
    ; Store 1 byte
    mov [rdi], al
    
.avx2_copy_done:
    ; SFENCE to ensure non-temporal stores are visible
    sfence
    
    jmp .avx2_done
    
.avx2_copy_unaligned:
    ; Unaligned copy using AVX2
    mov rcx, rdx
    shr rcx, 5  ; count / 32
    jz .avx2_copy_unaligned_16_bytes
    
.avx2_copy_unaligned_loop:
    ; Load 32 bytes using unaligned AVX2
    vmovdqu ymm0, [rsi]
    
    ; Store 32 bytes using unaligned AVX2
    vmovdqu [rdi], ymm0
    
    add rsi, 32
    add rdi, 32
    
    dec rcx
    jnz .avx2_copy_unaligned_loop
    
.avx2_copy_unaligned_16_bytes:
    ; Process remaining 16 bytes
    mov rcx, rdx
    shr rcx, 4  ; count / 16
    and rcx, 1  ; Check if we have 16 bytes remaining
    jz .avx2_copy_unaligned_8_bytes
    
.avx2_copy_unaligned_16_bytes_loop:
    ; Load 16 bytes using unaligned SSE
    movdqu xmm0, [rsi]
    
    ; Store 16 bytes using unaligned SSE
    movdqu [rdi], xmm0
    
    add rsi, 16
    add rdi, 16
    
.avx2_copy_unaligned_8_bytes:
    ; Process remaining 8 bytes
    mov rcx, rdx
    shr rcx, 3  ; count / 8
    and rcx, 1  ; Check if we have 8 bytes remaining
    jz .avx2_copy_unaligned_4_bytes
    
.avx2_copy_unaligned_8_bytes_loop:
    ; Load 8 bytes
    mov rax, [rsi]
    
    ; Store 8 bytes
    mov [rdi], rax
    
    add rsi, 8
    add rdi, 8
    
.avx2_copy_unaligned_4_bytes:
    ; Process remaining 4 bytes
    mov rcx, rdx
    shr rcx, 2  ; count / 4
    and rcx, 1  ; Check if we have 4 bytes remaining
    jz .avx2_copy_unaligned_2_bytes
    
.avx2_copy_unaligned_4_bytes_loop:
    ; Load 4 bytes
    mov eax, [rsi]
    
    ; Store 4 bytes
    mov [rdi], eax
    
    add rsi, 4
    add rdi, 4
    
.avx2_copy_unaligned_2_bytes:
    ; Process remaining 2 bytes
    mov rcx, rdx
    shr rcx, 1  ; count / 2
    and rcx, 1  ; Check if we have 2 bytes remaining
    jz .avx2_copy_unaligned_1_byte
    
.avx2_copy_unaligned_2_bytes_loop:
    ; Load 2 bytes
    mov ax, [rsi]
    
    ; Store 2 bytes
    mov [rdi], ax
    
    add rsi, 2
    add rdi, 2
    
.avx2_copy_unaligned_1_byte:
    ; Process remaining byte
    and rdx, 1  ; count % 2
    jz .avx2_copy_done
    
    ; Load 1 byte
    mov al, [rsi]
    
    ; Store 1 byte
    mov [rdi], al
    
    jmp .avx2_copy_done
    
.avx2_copy_small:
    ; Copy small blocks using optimized byte-by-byte copy
    mov rcx, rdx
    rep movsb
    
    jmp .avx2_done
    
.avx2_done:
    ; Restore registers
    pop rsi
    pop rcx
    pop rbx
    
    ; Return destination pointer
    mov rax, rdi
    ret

; Ultra-fast memcpy using AVX-512
memcpy_asm_avx512:
    ; Save registers we will use
    push rbx
    push rcx
    push rsi
    
    ; rdi = dest
    ; rsi = src
    ; rdx = count
    
    ; Check for zero count
    test rdx, rdx
    jz .avx512_done
    
    ; Check for AVX-512 support
    push rbx
    mov eax, 7
    xor ecx, ecx
    cpuid
    mov rbx, rax  ; Save leaf 7 sub-leaf 0 result
    mov eax, 1
    cpuid
    test ecx, 1 << 28  ; Check AVX-512 bit
    jz memcpy_asm_avx2  ; Fall back to AVX2 implementation
    
    ; Check for small counts
    cmp rdx, 512
    jb .avx512_copy_small
    
    ; Check memory alignment for optimal performance
    test rdi, 63
    jnz .avx512_copy_unaligned
    
    test rsi, 63
    jnz .avx512_copy_unaligned
    
    ; Aligned copy using AVX-512 with non-temporal stores for large copies
    mov rcx, rdx
    shr rcx, 9  ; count / 512
    jz .avx512_copy_256_bytes
    
.avx512_copy_512_bytes_loop:
    ; Load 512 bytes using AVX-512
    vmovdqa64 zmm0, [rsi]
    vmovdqa64 zmm1, [rsi + 64]
    vmovdqa64 zmm2, [rsi + 128]
    vmovdqa64 zmm3, [rsi + 192]
    vmovdqa64 zmm4, [rsi + 256]
    vmovdqa64 zmm5, [rsi + 320]
    vmovdqa64 zmm6, [rsi + 384]
    vmovdqa64 zmm7, [rsi + 448]
    
    ; Use non-temporal stores to avoid polluting the cache
    vmovntdq [rdi], zmm0
    vmovntdq [rdi + 64], zmm1
    vmovntdq [rdi + 128], zmm2
    vmovntdq [rdi + 192], zmm3
    vmovntdq [rdi + 256], zmm4
    vmovntdq [rdi + 320], zmm5
    vmovntdq [rdi + 384], zmm6
    vmovntdq [rdi + 448], zmm7
    
    add rsi, 512
    add rdi, 512
    
    dec rcx
    jnz .avx512_copy_512_bytes_loop
    
    ; Process remaining 256 bytes
    mov rcx, rdx
    shr rcx, 8  ; count / 256
    and rcx, 1  ; Check if we have 256 bytes remaining
    jz .avx512_copy_128_bytes
    
.avx512_copy_256_bytes:
    ; Load 256 bytes using AVX-512
    vmovdqa64 zmm0, [rsi]
    vmovdqa64 zmm1, [rsi + 64]
    vmovdqa64 zmm2, [rsi + 128]
    vmovdqa64 zmm3, [rsi + 192]
    
    ; Use non-temporal stores
    vmovntdq [rdi], zmm0
    vmovntdq [rdi + 64], zmm1
    vmovntdq [rdi + 128], zmm2
    vmovntdq [rdi + 192], zmm3
    
    add rsi, 256
    add rdi, 256
    
.avx512_copy_128_bytes:
    ; Process remaining 128 bytes
    mov rcx, rdx
    shr rcx, 7  ; count / 128
    and rcx, 1  ; Check if we have 128 bytes remaining
    jz .avx512_copy_64_bytes
    
.avx512_copy_128_bytes_loop:
    ; Load 128 bytes using AVX-512
    vmovdqa64 zmm0, [rsi]
    vmovdqa64 zmm1, [rsi + 64]
    
    ; Use non-temporal stores
    vmovntdq [rdi], zmm0
    vmovntdq [rdi + 64], zmm1
    
    add rsi, 128
    add rdi, 128
    
.avx512_copy_64_bytes:
    ; Process remaining 64 bytes
    mov rcx, rdx
    shr rcx, 6  ; count / 64
    and rcx, 1  ; Check if we have 64 bytes remaining
    jz .avx512_copy_32_bytes
    
.avx512_copy_64_bytes_loop:
    ; Load 64 bytes using AVX-512
    vmovdqa64 zmm0, [rsi]
    
    ; Use non-temporal stores
    vmovntdq [rdi], zmm0
    
    add rsi, 64
    add rdi, 64
    
.avx512_copy_32_bytes:
    ; Process remaining 32 bytes
    mov rcx, rdx
    shr rcx, 5  ; count / 32
    and rcx, 1  ; Check if we have 32 bytes remaining
    jz .avx512_copy_16_bytes
    
.avx512_copy_32_bytes_loop:
    ; Load 32 bytes using AVX2
    vmovdqa ymm0, [rsi]
    
    ; Use non-temporal stores
    vmovntdq [rdi], ymm0
    
    add rsi, 32
    add rdi, 32
    
.avx512_copy_16_bytes:
    ; Process remaining 16 bytes
    mov rcx, rdx
    shr rcx, 4  ; count / 16
    and rcx, 1  ; Check if we have 16 bytes remaining
    jz .avx512_copy_8_bytes
    
.avx512_copy_16_bytes_loop:
    ; Load 16 bytes using SSE
    movdqa xmm0, [rsi]
    
    ; Use non-temporal stores
    movntdq [rdi], xmm0
    
    add rsi, 16
    add rdi, 16
    
.avx512_copy_8_bytes:
    ; Process remaining 8 bytes
    mov rcx, rdx
    shr rcx, 3  ; count / 8
    and rcx, 1  ; Check if we have 8 bytes remaining
    jz .avx512_copy_4_bytes
    
.avx512_copy_8_bytes_loop:
    ; Load 8 bytes
    mov rax, [rsi]
    
    ; Store 8 bytes
    mov [rdi], rax
    
    add rsi, 8
    add rdi, 8
    
.avx512_copy_4_bytes:
    ; Process remaining 4 bytes
    mov rcx, rdx
    shr rcx, 2  ; count / 4
    and rcx, 1  ; Check if we have 4 bytes remaining
    jz .avx512_copy_2_bytes
    
.avx512_copy_4_bytes_loop:
    ; Load 4 bytes
    mov eax, [rsi]
    
    ; Store 4 bytes
    mov [rdi], eax
    
    add rsi, 4
    add rdi, 4
    
.avx512_copy_2_bytes:
    ; Process remaining 2 bytes
    mov rcx, rdx
    shr rcx, 1  ; count / 2
    and rcx, 1  ; Check if we have 2 bytes remaining
    jz .avx512_copy_1_byte
    
.avx512_copy_2_bytes_loop:
    ; Load 2 bytes
    mov ax, [rsi]
    
    ; Store 2 bytes
    mov [rdi], ax
    
    add rsi, 2
    add rdi, 2
    
.avx512_copy_1_byte:
    ; Process remaining byte
    and rdx, 1  ; count % 2
    jz .avx512_copy_done
    
    ; Load 1 byte
    mov al, [rsi]
    
    ; Store 1 byte
    mov [rdi], al
    
.avx512_copy_done:
    ; SFENCE to ensure non-temporal stores are visible
    sfence
    
    jmp .avx512_done
    
.avx512_copy_unaligned:
    ; Unaligned copy using AVX-512
    mov rcx, rdx
    shr rcx, 6  ; count / 64
    jz .avx512_copy_unaligned_32_bytes
    
.avx512_copy_unaligned_loop:
    ; Load 64 bytes using unaligned AVX-512
    vmovdqu64 zmm0, [rsi]
    
    ; Store 64 bytes using unaligned AVX-512
    vmovdqu64 [rdi], zmm0  ; تم تعديل هذا السطر
    
    add rsi, 64
    add rdi, 64
    
    dec rcx
    jnz .avx512_copy_unaligned_loop
    
.avx512_copy_unaligned_32_bytes:
    ; Process remaining 32 bytes
    mov rcx, rdx
    shr rcx, 5  ; count / 32
    and rcx, 1  ; Check if we have 32 bytes remaining
    jz .avx512_copy_unaligned_16_bytes
    
.avx512_copy_unaligned_32_bytes_loop:
    ; Load 32 bytes using unaligned AVX2
    vmovdqu ymm0, [rsi]
    
    ; Store 32 bytes using unaligned AVX2
    vmovdqu [rdi], ymm0
    
    add rsi, 32
    add rdi, 32
    
.avx512_copy_unaligned_16_bytes:
    ; Process remaining 16 bytes
    mov rcx, rdx
    shr rcx, 4  ; count / 16
    and rcx, 1  ; Check if we have 16 bytes remaining
    jz .avx512_copy_unaligned_8_bytes
    
.avx512_copy_unaligned_16_bytes_loop:
    ; Load 16 bytes using unaligned SSE
    movdqu xmm0, [rsi]
    
    ; Store 16 bytes using unaligned SSE
    movdqu [rdi], xmm0
    
    add rsi, 16
    add rdi, 16
    
.avx512_copy_unaligned_8_bytes:
    ; Process remaining 8 bytes
    mov rcx, rdx
    shr rcx, 3  ; count / 8
    and rcx, 1  ; Check if we have 8 bytes remaining
    jz .avx512_copy_unaligned_4_bytes
    
.avx512_copy_unaligned_8_bytes_loop:
    ; Load 8 bytes
    mov rax, [rsi]
    
    ; Store 8 bytes
    mov [rdi], rax
    
    add rsi, 8
    add rdi, 8
    
.avx512_copy_unaligned_4_bytes:
    ; Process remaining 4 bytes
    mov rcx, rdx
    shr rcx, 2  ; count / 4
    and rcx, 1  ; Check if we have 4 bytes remaining
    jz .avx512_copy_unaligned_2_bytes
    
.avx512_copy_unaligned_4_bytes_loop:
    ; Load 4 bytes
    mov eax, [rsi]
    
    ; Store 4 bytes
    mov [rdi], eax
    
    add rsi, 4
    add rdi, 4
    
.avx512_copy_unaligned_2_bytes:
    ; Process remaining 2 bytes
    mov rcx, rdx
    shr rcx, 1  ; count / 2
    and rcx, 1  ; Check if we have 2 bytes remaining
    jz .avx512_copy_unaligned_1_byte
    
.avx512_copy_unaligned_2_bytes_loop:
    ; Load 2 bytes
    mov ax, [rsi]
    
    ; Store 2 bytes
    mov [rdi], ax
    
    add rsi, 2
    add rdi, 2
    
.avx512_copy_unaligned_1_byte:
    ; Process remaining byte
    and rdx, 1  ; count % 2
    jz .avx512_copy_done
    
    ; Load 1 byte
    mov al, [rsi]
    
    ; Store 1 byte
    mov [rdi], al
    
    jmp .avx512_copy_done
    
.avx512_copy_small:
    ; Copy small blocks using optimized byte-by-byte copy
    mov rcx, rdx
    rep movsb
    
    jmp .avx512_done
    
.avx512_done:
    ; Restore registers
    pop rsi
    pop rcx
    pop rbx
    
    ; Return destination pointer
    mov rax, rdi
    ret
