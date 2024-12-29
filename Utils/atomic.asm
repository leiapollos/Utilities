.code

PUBLIC InterlockedOr64
; InterlockedOr64(ptr, value)
; Atomically performs a bitwise OR on *ptr with value.
; Returns the original value of *ptr.

InterlockedOr64 PROC
    ; RCX - long* ptr
    ; RDX - long value
    ; RAX - original value

    mov rax, [rcx]                  ; Load original value
    lock or qword ptr [rcx], rdx    ; Atomically OR with value
    ret
InterlockedOr64 ENDP

PUBLIC InterlockedExchange64
; InterlockedExchange64(ptr, value)
; Atomically sets *ptr to value.
; Returns the original value of *ptr.

InterlockedExchange64 PROC
    ; RCX - long* ptr
    ; RDX - long value
    ; RAX - original value

    lock xchg qword ptr [rcx], rdx  ; Atomically exchange values
    mov rax, rdx                     ; Move original value to RAX
    ret
InterlockedExchange64 ENDP

PUBLIC InterlockedCompareExchange64
; InterlockedCompareExchange64(ptr, exchange, comparand)
; Atomically compares *ptr with comparand.
; If equal, sets *ptr to exchange.
; Returns the original value of *ptr.

InterlockedCompareExchange64 PROC
    ; RCX - long* ptr
    ; RDX - long exchange
    ; R8  - long comparand
    ; RAX - original value

    mov rax, [rcx]                    ; Load original value
    lock cmpxchg qword ptr [rcx], rdx ; Compare and possibly exchange
    ret
InterlockedCompareExchange64 ENDP

END
