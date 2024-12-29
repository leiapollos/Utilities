.CODE

PUBLIC _mm_pause
; _mm_pause()
; Executes the PAUSE instruction to optimize spin-wait loops.
; Returns: None

_mm_pause PROC
    pause    ; Execute PAUSE instruction
    ret      ; Return to caller
_mm_pause ENDP

END
