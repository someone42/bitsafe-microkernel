/** \file syscall.h
  *
  * \brief Describes stuff that's common to both userspace and kernelspace.
  *
  * This file is licensed as described by the file LICENCE.
  */

#ifndef SYSCALL_H_INCLUDED
#define SYSCALL_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

/** Highest pre-emptive priority for a user thread. Cortex-M4F has 16 levels,
  * but the highest level is reserved for the kernel.
  * Note that smaller priority numbers mean higher priority. */
#define PRIORITY_HIGHEST        1
/** Lowest pre-emptive priority for a user thread. Cortex-M4F has 16 levels,
  * but the lowest level is reserved for the idle thread.
  * Note that bigger priority numbers mean lower priority. */
#define PRIORITY_LOWEST         14
/** Pre-emptive priority of the idle thread. */
#define PRIORITY_IDLE           15

typedef enum KernelReturnEnum
{
    /** Everything is fine, move along. */
    KERNEL_RETURN_SUCCESS               = 0,
    /** For the sake of simplicity, memory resources are statically allocated.
      * This means that after enough "open" calls for a shared resource,
      * it is possible to run out of memory. If that happens, this return
      * value will be returned by the "open" call. */
    KERNEL_RETURN_GLOBAL_NO_FREE        = 1,
    /** Since there are a limited number of shared kernel objects (eg. queues),
      * each process is only allowed to open a fraction of the total allowed
      * (eg. see #QUEUES_PER_PROCESS in queue.c). If a process attempts to open
      * more objects than that, the "open" call will return this return
      * value. */
    KERNEL_RETURN_PROCESS_NO_FREE       = 2,
    /** Invalid queue handle. Some situations where this can occur:
      * - Attempting to use a queue handle after it has been closed. Note that
      *   the caller of an inter-process call can asynchronously close a
      *   handle (even if the callee is still using it).
      * - Attempting to use a queue handle in a way that the current process
      *   does not have permission for. The "bad handle" return value is
      *   used for permissions errors to limit leaking information about which
      *   queue handles are open.
      */
    KERNEL_RETURN_BAD_HANDLE            = 3,
    /** Nothing went wrong, but the thread had to wait for something to happen.
      * If you get this return value, call the function again with exactly
      * the same arguments. */
    KERNEL_RETURN_WAIT                  = 4,
    /** Unrecognised system call. */
    KERNEL_RETURN_BAD_CALL              = 5,
    /** Invalid/out-of-range parameter provided to a kernel function. */
    KERNEL_RETURN_INVALID_PARAM         = 6,
    /** Too many of something in a semaphore function. This will typically be
      * returned if there is an imbalance of waits/signals. */
    KERNEL_RETURN_TOO_MANY              = 7,
    /** Bad priority supplied to scheduler function. This will typically be
      * returned if a thread tries to set its priority too high. */
    KERNEL_RETURN_BAD_PRIORITY          = 8,
    /** Current process doesn't have access to the specified IRQ. */
    KERNEL_RETURN_IRQ_PERMISSION        = 9
} KernelReturn;

typedef enum SysCallNumEnum
{
    SYSCALL_EVENT_INIT          = 1,
    SYSCALL_EVENT_WAIT          = 2,
    SYSCALL_EVENT_SET           = 3,
    SYSCALL_EVENT_RESET         = 4,
    SYSCALL_SEM_INIT            = 5,
    SYSCALL_SEM_WAIT            = 6,
    SYSCALL_SEM_SIGNAL          = 7,
    SYSCALL_THREAD_CREATE       = 8,
    SYSCALL_THREAD_EXIT         = 9,
    SYSCALL_THREAD_SLEEP        = 10,
    SYSCALL_QUEUE_OPEN          = 11,
    SYSCALL_QUEUE_CLOSE         = 12,
    SYSCALL_QUEUE_WRITE         = 13,
    SYSCALL_QUEUE_READ          = 14,
    SYSCALL_IPC_CALL            = 15,
    SYSCALL_IPC_HANDLE          = 16,
    SYSCALL_INT_WAIT            = 17,
    SYSCALL_INT_ENABLE          = 18,
    SYSCALL_INT_DISABLE         = 19,
    SYSCALL_INT_ACKNOWLEDGE     = 20
} SysCallNum;

#endif // #ifndef SYSCALL_H_INCLUDED
