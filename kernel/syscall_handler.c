/** \file syscall_handler.c
  *
  * \brief Handles system calls from unprivileged code.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"
#include "check_access.h"

/** Initialise system call handler. This should be called before any system
  * calls are done. */
void ksyscallInit(void)
{
    // Sanity check for sizeof(KernelEvent) and sizeof(KernelSemaphore).
    // SVC_Handler() does some checking and assumes the following:
    KERNEL_ASSERT(sizeof(KernelEvent) == sizeof(uint8_t));
    KERNEL_ASSERT(sizeof(KernelSemaphore) <= sizeof(uint32_t));
    KERNEL_ASSERT(sizeof(QueueHandle) <= sizeof(uint32_t));
    // If you do end up messing with these, ensure that the typedefs for
    // UserEvent, UserSemaphore and UserQueueHandle in userspace_lib.h are
    // also updated.
}

/** SVCall (supervisor call) exception handler. This is the only way for
  * unprivileged (userspace) code to call kernel functions.
  * \param call_num System call number. Should be one of #SysCallNum.
  * \param arg0 First argument to system call (if any).
  * \param arg1 Second argument to system call (if any).
  * \param more_args Additional arguments to system call (if any). The return
  *                  value of the system call will also be written here, so
  *                  this must be a valid pointer.
  */
void SVC_Handler(uint32_t call_num, uint32_t arg0, uint32_t arg1, uint32_t *more_args)
{
    KernelReturn return_value;

    // Be very careful with pointers from userspace. All pointer accesses
    // must be checked, otherwise privilege escalation exploits will ensue.
    // Most of the checks are done here, but a couple of the more complicated
    // functions do their own checking.
    switch (call_num)
    {
    case SYSCALL_EVENT_INIT:
        checkAccessByte((uint8_t *)arg0);
        keventInit((KernelEvent *)arg0, (bool)arg1);
        return_value = KERNEL_RETURN_SUCCESS;
        break;
    case SYSCALL_EVENT_WAIT:
        checkAccessByte((uint8_t *)arg0);
        keventWait((KernelEvent *)arg0);
        return_value = KERNEL_RETURN_SUCCESS;
        break;
    case SYSCALL_EVENT_SET:
        checkAccessByte((uint8_t *)arg0);
        keventSet((KernelEvent *)arg0);
        return_value = KERNEL_RETURN_SUCCESS;
        break;
    case SYSCALL_EVENT_RESET:
        checkAccessByte((uint8_t *)arg0);
        keventReset((KernelEvent *)arg0);
        return_value = KERNEL_RETURN_SUCCESS;
        break;
    case SYSCALL_SEM_INIT:
        checkAccessWord((uint32_t *)arg0);
        return_value = ksemInit((KernelSemaphore *)arg0, (int8_t)arg1);
        break;
    case SYSCALL_SEM_WAIT:
        checkAccessWord((uint32_t *)arg0);
        return_value = ksemWait((KernelSemaphore *)arg0);
        break;
    case SYSCALL_SEM_SIGNAL:
        checkAccessWord((uint32_t *)arg0);
        return_value = ksemSignal((KernelSemaphore *)arg0);
        break;
    case SYSCALL_THREAD_CREATE:
        // kschedCreateThread() checks all of its pointer accesses.
        return_value = kschedCreateThread((void (*)(void *))arg0,
                                          (uint32_t *)arg1,
                                          (void *)loadCheckedWord(&(more_args[0])),
                                          (uint8_t)loadCheckedWord(&(more_args[1])));
        break;
    case SYSCALL_THREAD_EXIT:
        kschedExitThread();
        return_value = KERNEL_RETURN_SUCCESS;
        break;
    case SYSCALL_THREAD_SLEEP:
        return_value = kschedSleep(arg0);
        break;
    case SYSCALL_QUEUE_OPEN:
        checkAccessWord((uint32_t *)arg1);
        return_value = kqueueOpen((uint8_t)arg0, (QueueHandle *)arg1);
        break;
    case SYSCALL_QUEUE_CLOSE:
        return_value = kqueueClose((QueueHandle)arg0);
        break;
    case SYSCALL_QUEUE_WRITE:
        // kqueueWrite() checks all of its pointer accesses.
        return_value = kqueueWrite((QueueHandle)arg0,
                                   (bool)arg1,
                                   (uint8_t *)loadCheckedWord(&(more_args[0])),
                                   (uint32_t)loadCheckedWord(&(more_args[1])),
                                   (uint32_t *)loadCheckedWord(&(more_args[2])));
        break;
    case SYSCALL_QUEUE_READ:
        // kqueueRead() checks all of its pointer accesses.
        return_value = kqueueRead((QueueHandle)arg0,
                                  (bool)arg1,
                                  (uint8_t *)loadCheckedWord(&(more_args[0])),
                                  (uint32_t)loadCheckedWord(&(more_args[1])),
                                  (uint32_t *)loadCheckedWord(&(more_args[2])));
        break;
    case SYSCALL_IPC_CALL:
        return_value = kprocessCall((uint8_t)arg0, (QueueHandle)arg1);
        break;
    case SYSCALL_IPC_HANDLE:
        checkAccessByte((uint8_t *)arg0);
        checkAccessWord((uint32_t *)arg1);
        return_value = kprocessHandleCall((uint8_t *)arg0, (QueueHandle *)arg1);
        break;
    case SYSCALL_INT_WAIT:
        return_value = kinterruptWait((uint8_t)arg0);
        break;
    case SYSCALL_INT_ENABLE:
        return_value = kinterruptEnable((uint8_t)arg0);
        break;
    case SYSCALL_INT_DISABLE:
        return_value = kinterruptDisable((uint8_t)arg0);
        break;
    case SYSCALL_INT_ACKNOWLEDGE:
        return_value = kinterruptAcknowledge((uint8_t)arg0, (bool)arg1);
        break;
    default:
        return_value = KERNEL_RETURN_BAD_CALL;
        break;
    }
    storeCheckedWord(&(more_args[0]), return_value);
}
