; json_fast.s - SIMD JSON structural tokenizer (SSE4.2 + AVX2)
; Finds structural chars in 16/32 byte chunks using SIMD

global json_fast_tokenizer
global json_fast_tokenizer_avx2

; ===== Read-only data =====
section .rodata align=16
sse_quote:       times 16 db 0x22
sse_brace_open:  times 16 db 0x7B
sse_brace_close: times 16 db 0x7D
sse_bracket_open: times 16 db 0x5B
sse_bracket_close: times 16 db 0x5D
sse_space:       times 16 db 0x20
sse_tab:         times 16 db 0x09
sse_lf:          times 16 db 0x0A
sse_cr:          times 16 db 0x0D

align 32
avx_quote:       times 32 db 0x22
avx_brace_open:  times 32 db 0x7B
avx_brace_close: times 32 db 0x7D
avx_bracket_open: times 32 db 0x5B
avx_bracket_close: times 32 db 0x5D
avx_space:       times 32 db 0x20
avx_tab:         times 32 db 0x09
avx_lf:          times 32 db 0x0A
avx_cr:          times 32 db 0x0D

section .text

; ===== SSE4.2 Tokenizer =====
; rdi = JSON string, rsi = length
; Returns token count (compatibility: 0)
json_fast_tokenizer:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi
    mov r13, rsi
    add r13, rdi
    xor r14d, r14d
    xor r15d, r15d

    cmp r12, r13
    jge .done

.main_loop:
    mov rax, r13
    sub rax, r12
    cmp rax, 16
    jb .scalar_loop

    cmp r14d, 1
    je .scalar_loop

    movdqu xmm0, [r12]
    xor eax, eax

    ; Compare with structural chars
    movdqa xmm1, [rel sse_quote]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    movdqa xmm1, [rel sse_brace_open]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    movdqa xmm1, [rel sse_brace_close]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    movdqa xmm1, [rel sse_bracket_open]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    movdqa xmm1, [rel sse_bracket_close]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    movdqa xmm1, [rel sse_space]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    movdqa xmm1, [rel sse_tab]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    movdqa xmm1, [rel sse_lf]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    movdqa xmm1, [rel sse_cr]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    or eax, edx

    test eax, eax
    jz .skip_16

    ; Process bits
    xor ecx, ecx
.bit_loop:
    bsf ecx, eax
    jz .done_block

    mov r15b, [r12 + rcx]

    cmp r14d, 1
    je .str_char

    cmp r15b, 0x22
    je .enter_str
    cmp r15b, 0x7B
    je .enter_obj
    cmp r15b, 0x7D
    je .leave_obj
    cmp r15b, 0x5B
    je .enter_arr
    cmp r15b, 0x5D
    je .leave_arr

    inc r15d

.next_bit:
    btr eax, ecx
    jmp .bit_loop

.done_block:
    lea r12, [r12 + rcx + 1]
    jmp .main_loop

.skip_16:
    lea r12, [r12 + 16]
    jmp .main_loop

.enter_str:
    mov r14d, 1
    inc r15d
    lea r12, [r12 + rcx + 1]
    jmp .main_loop

.leave_str:
    xor r14d, r14d
    inc r15d
    lea r12, [r12 + rcx + 1]
    jmp .main_loop

.enter_obj:
    mov r14d, 2
    inc r15d
    lea r12, [r12 + rcx + 1]
    jmp .main_loop

.leave_obj:
    xor r14d, r14d
    inc r15d
    lea r12, [r12 + rcx + 1]
    jmp .main_loop

.enter_arr:
    mov r14d, 3
    inc r15d
    lea r12, [r12 + rcx + 1]
    jmp .main_loop

.leave_arr:
    xor r14d, r14d
    inc r15d
    lea r12, [r12 + rcx + 1]
    jmp .main_loop

.str_char:
    cmp r15b, 0x22
    je .leave_str
    cmp r15b, 0x5C
    je .esc
    inc r15d
    jmp .next_bit

.esc:
    lea r12, [r12 + rcx + 2]
    cmp r12, r13
    jge .done
    jmp .main_loop

; ===== Scalar fallback =====
.scalar_loop:
    cmp r12, r13
    jge .done
    mov r15b, [r12]

    cmp r14d, 1
    je .scalar_str

    cmp r15b, 0x22
    je .scalar_enter_str
    cmp r15b, 0x7B
    je .scalar_enter_obj
    cmp r15b, 0x7D
    je .scalar_leave_obj
    cmp r15b, 0x5B
    je .scalar_enter_arr
    cmp r15b, 0x5D
    je .scalar_leave_arr
    cmp r15b, 0x20
    je .scalar_ws
    cmp r15b, 0x09
    je .scalar_ws
    cmp r15b, 0x0A
    je .scalar_ws
    cmp r15b, 0x0D
    je .scalar_ws

    inc r12
    jmp .scalar_loop

.scalar_enter_str:
    mov r14d, 1
    inc r15d
    inc r12
    jmp .scalar_loop

.scalar_leave_str:
    xor r14d, r14d
    inc r15d
    inc r12
    jmp .scalar_loop

.scalar_enter_obj:
    mov r14d, 2
    inc r15d
    inc r12
    jmp .scalar_loop

.scalar_leave_obj:
    xor r14d, r14d
    inc r15d
    inc r12
    jmp .scalar_loop

.scalar_enter_arr:
    mov r14d, 3
    inc r15d
    inc r12
    jmp .scalar_loop

.scalar_leave_arr:
    xor r14d, r14d
    inc r15d
    inc r12
    jmp .scalar_loop

.scalar_ws:
    inc r15d
    inc r12
    jmp .scalar_loop

.scalar_str:
    cmp r15b, 0x22
    je .scalar_leave_str
    cmp r15b, 0x5C
    je .scalar_esc
    inc r12
    jmp .scalar_loop

.scalar_esc:
    inc r12
    inc r12
    cmp r12, r13
    jge .done
    jmp .scalar_loop

.done:
    xor eax, eax
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

; ===== AVX2 Tokenizer =====
json_fast_tokenizer_avx2:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi
    mov r13, rsi
    add r13, rdi
    xor r14d, r14d
    xor r15d, r15d

    cmp r12, r13
    jge .avx_done

.avx_main:
    mov rax, r13
    sub rax, r12
    cmp rax, 32
    jb json_fast_tokenizer.main_loop

    cmp r14d, 1
    je json_fast_tokenizer.main_loop

    vmovdqu ymm0, [r12]
    xor eax, eax

    vpcmpeqb ymm1, ymm0, [rel avx_quote]
    vpmovmskb edx, ymm1
    or eax, edx

    vpcmpeqb ymm1, ymm0, [rel avx_brace_open]
    vpmovmskb edx, ymm1
    or eax, edx

    vpcmpeqb ymm1, ymm0, [rel avx_brace_close]
    vpmovmskb edx, ymm1
    or eax, edx

    vpcmpeqb ymm1, ymm0, [rel avx_bracket_open]
    vpmovmskb edx, ymm1
    or eax, edx

    vpcmpeqb ymm1, ymm0, [rel avx_bracket_close]
    vpmovmskb edx, ymm1
    or eax, edx

    vpcmpeqb ymm1, ymm0, [rel avx_space]
    vpmovmskb edx, ymm1
    or eax, edx

    vpcmpeqb ymm1, ymm0, [rel avx_tab]
    vpmovmskb edx, ymm1
    or eax, edx

    vpcmpeqb ymm1, ymm0, [rel avx_lf]
    vpmovmskb edx, ymm1
    or eax, edx

    vpcmpeqb ymm1, ymm0, [rel avx_cr]
    vpmovmskb edx, ymm1
    or eax, edx

    test eax, eax
    jz .avx_skip

    jmp json_fast_tokenizer.main_loop

.avx_skip:
    lea r12, [r12 + 32]
    jmp .avx_main

.avx_done:
    xor eax, eax
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret
