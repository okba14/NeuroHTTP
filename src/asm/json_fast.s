; json_fast.s - JSON tokenizer مكتوب بلغة التجميع
; يعتمد على معمارية x86-64

.global json_fast_tokenizer

; دالة: json_fast_tokenizer
; المدخلات: 
;   rdi - مؤشر إلى سلسلة JSON
;   rsi - طول السلسلة
; المخرجات:
;   لا شيء (سيتم تعديل الذاكرة مباشرة)

json_fast_tokenizer:
    pushq %rbp
    movq %rsp, %rbp
    
    ; حفظ السجلات التي سنستخدمها
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    
    ; rdi = json_string
    ; rsi = length
    ; r12 = current position
    ; r13 = end position
    ; r14 = state (0: outside, 1: in string, 2: in object, 3: in array)
    
    movq %rdi, %r12          ; current position = start of string
    movq %rsi, %r13           ; end position = length
    movq $0, %r14             ; state = outside
    
    ; حلقة المعالجة الرئيسية
.loop:
    cmpq %r12, %r13           ; if current_position >= end_position, exit
    jge .end
    
    movb (%r12), %al          ; load current character
    
    ; معالجة الحالة الحالية
    cmpq $0, %r14             ; if state == outside
    je .outside
    
    cmpq $1, %r14             ; if state == in_string
    je .in_string
    
    cmpq $2, %r14             ; if state == in_object
    je .in_object
    
    cmpq $3, %r14             ; if state == in_array
    je .in_array
    
.outside:
    ; معالجة الأحرف خارج النصوص
    cmpb $'"', %al            ; if char == '"', start string
    je .start_string
    
    cmpb $'{', %al            ; if char == '{', start object
    je .start_object
    
    cmpb $'[', %al            ; if char == '[', start array
    je .start_array
    
    cmpb $'}', %al            ; if char == '}', end object
    je .end_object
    
    cmpb $']', %al            ; if char == ']', end array
    je .end_array
    
    jmp .next_char            ; otherwise, skip
    
.in_string:
    ; معالجة الأحرف داخل النص
    cmpb $'"', %al            ; if char == '"', end string
    je .end_string
    
    cmpb $'\\', %al           ; if char == '\', handle escape
    je .escape_char
    
    jmp .next_char            ; otherwise, continue
    
.in_object:
    ; معالجة الأحرف داخل الكائن
    cmpb $'}', %al            ; if char == '}', end object
    je .end_object
    
    cmpb $'"', %al            ; if char == '"', start string
    je .start_string
    
    jmp .next_char            ; otherwise, continue
    
.in_array:
    ; معالجة الأحرف داخل المصفوفة
    cmpb $']', %al            ; if char == ']', end array
    je .end_array
    
    cmpb $'"', %al            ; if char == '"', start string
    je .start_string
    
    jmp .next_char            ; otherwise, continue
    
.start_string:
    movq $1, %r14             ; state = in_string
    jmp .next_char
    
.end_string:
    movq $0, %r14             ; state = outside
    jmp .next_char
    
.start_object:
    movq $2, %r14             ; state = in_object
    jmp .next_char
    
.end_object:
    movq $0, %r14             ; state = outside
    jmp .next_char
    
.start_array:
    movq $3, %r14             ; state = in_array
    jmp .next_char
    
.end_array:
    movq $0, %r14             ; state = outside
    jmp .next_char
    
.escape_char:
    ; تخطي الحرف التالي بعد '\'
    incq %r12
    jmp .next_char
    
.next_char:
    incq %r12                 ; current_position++
    jmp .loop
    
.end:
    ; استعادة السجلات
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    
    movq %rbp, %rsp
    popq %rbp
    ret
