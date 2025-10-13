; crc32.s - حساب CRC32 محسن بلغة التجميع
; يعتمد على معمارية x86-64 مع دعم SSE4.2

.global crc32_asm

; دالة: crc32_asm
; المدخلات: 
;   rdi - مؤشر إلى البيانات
;   rsi - طول البيانات
; المخرجات:
;   eax - قيمة CRC32

crc32_asm:
    ; حفظ السجلات التي سنستخدمها
    pushq %rbx
    pushq %rcx
    pushq %rdx
    
    ; rdi = data
    ; rsi = length
    
    ; التحقق من دعم SSE4.2
    movq $1, %rax
    cpuid
    movq %rbx, %rcx
    shrq $20, %rcx  ; تحقق من بت SSE4.2
    testq $1, %rcx
    jz .no_sse42
    
    ; استخدام تعليمات CRC32 SSE4.2
    movq %rdi, %rdx
    movq %rsi, %rcx
    xorl %eax, %eax  ; تهيئة CRC بـ 0
    
    ; معالجة 8 بايت في كل مرة
    movq %rcx, %rbx
    shrq $3, %rbx  ; length / 8
    jz .process_bytes
    
.crc_loop:
    crc32q (%rdx), %rax
    
    addq $8, %rdx
    
    decq %rbx
    jnz .crc_loop
    
    ; معالجة البايتات المتبقية
    andq $7, %rcx  ; length % 8
    jz .done_sse
    
.process_bytes:
    movb (%rdx), %bl
    crc32b %bl, %eax
    
    incq %rdx
    
    decq %rcx
    jnz .process_bytes
    
.done_sse:
    ; نفي النتيجة (تطابق مع تنفيذ CRC32 القياسي)
    notl %eax
    
    jmp .done
    
.no_sse42:
    ; تنفيذ CRC32 البرمجي (تبسيط)
    movq %rdi, %rdx
    movq %rsi, %rcx
    movl $0xFFFFFFFF, %eax  ; تهيئة CRC بـ 0xFFFFFFFF
    
.software_loop:
    movb (%rdx), %bl
    xorl %ebx, %eax
    
    ; 8 تكرارات لكل بايت
    movl $8, %edx
.software_inner:
    shrl $1, %eax
    jnc .no_xor
    xorl $0xEDB88320, %eax
.no_xor:
    decq %rdx
    jnz .software_inner
    
    incq %rdx
    
    decq %rcx
    jnz .software_loop
    
    ; نفي النتيجة
    notl %eax
    
.done:
    ; استعادة السجلات
    popq %rdx
    popq %rcx
    popq %rbx
    
    ret
