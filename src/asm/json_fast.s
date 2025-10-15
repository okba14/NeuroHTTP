; json_fast.s - JSON tokenizer written in assembly language
; Based on x86-64 architecture

global json_fast_tokenizer

; Function: json_fast_tokenizer
; Inputs:
;   rdi - pointer to JSON string
;   rsi - string length
; Outputs:
;   Nothing (memory will be modified directly)

section .text

json_fast_tokenizer:
    push rbp
    mov rbp, rsp
    
    ; Save registers we will use
    push rbx
    push r12
    push r13
    push r14
    
    ; rdi = json_string
    ; rsi = length
    ; r12 = current position
    ; r13 = end position
    ; r14 = state (0: outside, 1: in string, 2: in object, 3: in array)
    
    mov r12, rdi          ; current position = start of string
    mov r13, rsi           ; end position = length
    mov r14, 0             ; state = outside
    
    ; Main processing loop
.loop:
    cmp r12, r13           ; if current_position >= end_position, exit
    jge .end
    
    mov al, [r12]          ; load current character
    
    ; Process current state
    cmp r14, 0             ; if state == outside
    je .outside
    
    cmp r14, 1             ; if state == in_string
    je .in_string
    
    cmp r14, 2             ; if state == in_object
    je .in_object
    
    cmp r14, 3             ; if state == in_array
    je .in_array
    
.outside:
    ; Process characters outside strings
    cmp al, '"'            ; if char == '"', start string
    je .start_string
    
    cmp al, '{'            ; if char == '{', start object
    je .start_object
    
    cmp al, '['            ; if char == '[', start array
    je .start_array
    
    cmp al, '}'            ; if char == '}', end object
    je .end_object
    
    cmp al, ']'            ; if char == ']', end array
    je .end_array
    
    jmp .next_char            ; otherwise, skip
    
.in_string:
    ; Process characters inside string
    cmp al, '"'            ; if char == '"', end string
    je .end_string
    
    cmp al, '\'            ; if char == '\', handle escape
    je .escape_char
    
    jmp .next_char            ; otherwise, continue
    
.in_object:
    ; Process characters inside object
    cmp al, '}'            ; if char == '}', end object
    je .end_object
    
    cmp al, '"'            ; if char == '"', start string
    je .start_string
    
    jmp .next_char            ; otherwise, continue
    
.in_array:
    ; Process characters inside array
    cmp al, ']'            ; if char == ']', end array
    je .end_array
    
    cmp al, '"'            ; if char == '"', start string
    je .start_string
    
    jmp .next_char            ; otherwise, continue
    
.start_string:
    mov r14, 1             ; state = in_string
    jmp .next_char
    
.end_string:
    mov r14, 0             ; state = outside
    jmp .next_char
    
.start_object:
    mov r14, 2             ; state = in_object
    jmp .next_char
    
.end_object:
    mov r14, 0             ; state = outside
    jmp .next_char
    
.start_array:
    mov r14, 3             ; state = in_array
    jmp .next_char
    
.end_array:
    mov r14, 0             ; state = outside
    jmp .next_char
    
.escape_char:
    ; Skip next character after '\'
    inc r12
    jmp .next_char
    
.next_char:
    inc r12                 ; current_position++
    jmp .loop
    
.end:
    ; Restore registers
    pop r14
    pop r13
    pop r12
    pop rbx
    
    mov rsp, rbp
    pop rbp
    ret
