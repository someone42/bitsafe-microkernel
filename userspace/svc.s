; svc.s
;
; Assembly-language thunk to execute SVC instruction.

  SECTION .text:CODE(2)
  THUMB

  PUBLIC supervisorCall
supervisorCall
  ; SVC causes a SVCall exception, causing the the processor to enter
  ; supervisor (i.e. kernel) mode. The immediate argument #0 is ignored.
  svc   #0
  bx    lr

  END
