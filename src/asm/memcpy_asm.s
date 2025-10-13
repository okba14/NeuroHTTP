; memcpy_asm.s - نسخ ذاكرة محسن بلغة التجميع
; يعتمد على معمارية x86-64

.global memcpy_asm

; دالة: memcpy_asm
; المدخلات: 
;   rdi - وجهة الذاكرة
;   rsi - مصدر الذاكرة
;   rdx - عدد البايتات لنسخها
; المخرجات:
;   rdi - مؤشر إلى وجهة الذاكرة

memcpy_asm:
    ; حفظ السجلات التي سنستخدمها
    pushq %rbx
    pushq %rcx
    pushq %rsi
    
    ; rdi = dest
    ; rsi = src
    ; rdx = count
    
    ; التحقق من العد الصفري
    testq %rdx, %rdx
    jz .done
    
    ; نسخ بايت واحد في كل مرة للعدد الصغير
    cmpq $16, %rdx
    jb .copy_byte
    
    ; التحقق من محاذاة الذاكرة
    testq $15, %rdi
    jnz .copy_unaligned
    
    ; نسخ 16 بايت في كل مرة (محاذاة)
    movq %rdx, %rcx
    shrq $4, %rcx  ; count / 16
    jz .copy_remaining
    
.copy_loop:
    movdqu (%rsi), %xmm0
    movdqu %xmm0, (%rdi)
    
    addq $16, %rsi
    addq $16, %rdi
    
    decq %rcx
    jnz .copy_loop
    
    ; نسخ البايتات المتبقية
    andq $15, %rdx  ; count % 16
    jz .done
    
.copy_remaining:
    movq %rdx, %rcx
.copy_remaining_loop:
    movb (%rsi), %al
    movb %al, (%rdi)
    
    incq %rsi
    incq %rdi
    
    decq %rcx
    jnz .copy_remaining_loop
    
    jmp .done
    
.copy_unaligned:
    ; نسخ 8 بايت في كل مرة (غير محاذاة)
    movq %rdx, %rcx
    shrq $3, %rcx  ; count / 8
    jz .copy_byte_remaining
    
.copy_unaligned_loop:
    movq (%rsi), %rax
    movq %rax, (%rdi)
    
    addq $8, %rsi
    addq $8, %rdi
    
    decq %rcx
    jnz .copy_unaligned_loop
    
    ; نسخ البايتات المتبقية
    andq $7, %rdx  ; count % 8
    jz .done
    
.copy_byte_remaining:
    movq %rdx, %rcx
.copy_byte_loop:
    movb (%rsi), %al
    movb %al, (%rdi)
    
    incq %rsi
    incq %rdi
    
    decq %rcx
    jnz .copy_byte_loop
    
    jmp .done
    
.copy_byte:
    ; نسخ بايت واحد في كل مرة
    movq %rdx, %rcx
.copy_byte_only:
    movb (%rsi), %al
    movb %al, (%rdi)
    
    incq %rsi
    incq %rdi
    
    decq %rcx
    jnz .copy_byte_only
    
.done:
    ; استعادة السجلات
    popq %rsi
    popq %rcx
    popq %rbx
    
    ; إرجاع مؤشر الوجهة
    movq %rdi, %rax
    ret
