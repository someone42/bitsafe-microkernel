; scheduler_asm.s
;
; Assembly portion of context switcher. It has to be in assembly because
; we're saving registers, and C code would probably clobber some of them.

  SECTION .text:CODE(2)
  THUMB

  EXTERN kschedSwitchThread

  PUBLIC PendSV_Handler
PendSV_Handler

  ; Get process stack, to save more stuff onto.
  mrs   r0, psp
  ; The Cortex-M4F only saves R0-R3, R12, LR, PC and PSR upon exception entry.
  ; All other registers need to be saved manually.
  ; LR is saved again because it is clobbered by the upcoming BL instruction.
  ; This extra LR is this handler's LR, not the thread's LR (the thread's LR
  ; has already been saved).
  stmdb r0!, {r4-r11, lr}

  ; Note: the FPU state is not saved, because it is assumed that the FPU isn't
  ; used.

  ; Updated stack pointer (in r0) will be saved by kschedSwitchThread().

  ; Get C code to do the rest of the context switch.
  bl    kschedSwitchThread

  ; r0 contains stack pointer of new thread.

  ; Do the inverse of everything above.
  ldmia r0!, {r4-r11, lr}
  msr   psp, r0

  bx    lr

  END
