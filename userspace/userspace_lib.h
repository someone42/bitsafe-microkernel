/** \file userspace_lib.h
  *
  * \brief Describes functions exported by userspace_lib.c
  *
  * This file is licensed as described by the file LICENCE.
  */

#ifndef USERSPACE_LIB_H
#define USERSPACE_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../syscall.h"

/** Convenience macro for using the memory region occupied by an array as a
  * Cortex-M4 stack. Since the Cortex-M4 uses a descending stack, this macro
  * obtains the location of the top of the stack.
  *
  * Note that the Cortex-M4 push operation decrements before storing, hence it
  * is safe to add sizeof(x) to the address of the variable; there's no danger
  * of clobbering variables in the memory immediately following x. */
#define ARRAY_AS_STACK(x)        ((uint32_t *)((uint8_t *)(x) + sizeof(x)))

/** This will end up effectively calling SVC_Handler()
  * (see syscall_handler.c).
  * \param call_num System call number. Should be one of #SysCallNum.
  * \param arg0 First argument to system call (if any).
  * \param arg1 Second argument to system call (if any).
  * \param more_args Additional arguments to system call (if any). The return
  *                  value of the system call will also be written here, so
  *                  this must be a valid pointer.
  */
extern void supervisorCall(uint32_t call_num, uint32_t arg0, uint32_t arg1, uint32_t *more_args);

// If messing with these, update the sanity checks in syscall_handle.c
// (ksyscallInit()).
typedef uint8_t UserEvent;
typedef uint32_t UserSemaphore;
typedef uint32_t UserQueueHandle;

// Event functions.
extern void ueventInit(UserEvent *event, bool auto_reset);
extern void ueventWait(UserEvent *event);
extern void ueventSet(UserEvent *event);
extern void ueventReset(UserEvent *event);

// Semaphores functions.
extern KernelReturn usemInit(UserSemaphore *sem, int8_t in_max);
extern KernelReturn usemSignal(UserSemaphore *sem);
extern KernelReturn usemWait(UserSemaphore *sem);

// Thread management functions.
extern KernelReturn uschedCreateThread(void (*entry)(void *), uint32_t *stack, void *arg, uint8_t priority);
extern void uschedExitThread(void);
extern KernelReturn uschedSleep(uint32_t ticks);

#ifndef NO_PROCESSES

// Inter-process queue functions.
extern KernelReturn uqueueOpen(uint8_t dest_process_id, UserQueueHandle *out_handle);
extern KernelReturn uqueueClose(UserQueueHandle handle);
extern KernelReturn uqueueWrite(UserQueueHandle handle, bool is_forward, uint8_t *data, uint32_t length);
extern KernelReturn uqueueRead(UserQueueHandle handle, bool is_forward, uint8_t *data, uint32_t size);

// Inter-process calling functions.
extern KernelReturn uprocessCall(uint8_t dest_process_id, UserQueueHandle transfer_queue);
extern void uprocessHandleCall(uint8_t *source_process_id, UserQueueHandle *transfer_queue);

// Interrupt management functions.
extern KernelReturn uinterruptWait(uint8_t irq);
extern KernelReturn uinterruptEnable(uint8_t irq);
extern KernelReturn uinterruptDisable(uint8_t irq);
extern KernelReturn uinterruptAcknowledge(uint8_t irq, bool is_level_sensitive);

#endif // #ifndef NO_PROCESSES

#ifdef __cplusplus
}
#endif

#endif // #ifndef USERSPACE_LIB_H
