; check_access_asm.s
;
; MPU access checking routines.
; These exploit the ARM Cortex-M4F "load unprivileged" and "store unprivileged"
; instructions to force the MPU to check that the current process is allowed
; to access certain regions of memory.
; Such checking is necessary because kernel-mode functions execute in
; privileged mode, but some syscalls use pointers (which originate from
; userspace).
;
; It's probably faster to do the loads/stores with inline assembly. But
; explicit function calls are an easy way to guarantee that the compiler
; won't pull any optimisation shenanigans.

  SECTION .text:CODE(2)
  THUMB

; Test for read/write access to a byte.
  PUBLIC checkAccessByte
checkAccessByte
  ldrbt r1, [r0, #0]
  strbt r1, [r0, #0]
  bx    lr

; Test for read/write access to a word (32 bits).
  PUBLIC checkAccessWord
checkAccessWord
  ldrt  r1, [r0, #0]
  strt  r1, [r0, #0]
  bx    lr

; Load a byte using an unprivileged load.
  PUBLIC loadCheckedByte
loadCheckedByte
  ldrbt r0, [r0, #0]
  bx    lr

; Load a word (32 bits) using an unprivileged load.
  PUBLIC loadCheckedWord
loadCheckedWord
  ldrt  r0, [r0, #0]
  bx    lr

; Store a byte using an unprivileged store.
  PUBLIC storeCheckedByte
storeCheckedByte
  strbt r1, [r0, #0]
  bx    lr

; Store a word (32 bits) using an unprivileged load.
  PUBLIC storeCheckedWord
storeCheckedWord
  strt  r1, [r0, #0]
  bx    lr

  END
