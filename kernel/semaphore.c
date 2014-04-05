/** \file semaphore.c
  *
  * \brief Kernel semaphores.
  *
  * Semaphores control access to shared resources.
  * For safety, it is not possible to delete semaphores.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"

/** Initialise a semaphore. Do not call this on an active semaphore, otherwise
  * the semaphore will end up in a weird state.
  * \param semaphore Semaphore structure to initialise.
  * \param in_max Maximum semaphore count (also the initial count). To create
  *               a mutex/lock, pass 1 for this parameter.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM
  *         if in_max was invalid.
  */
KernelReturn ksemInit(KernelSemaphore *sem, int8_t in_max)
{
    if (in_max < 1)
    {
        return KERNEL_RETURN_INVALID_PARAM; // invalid in_max value
    }
    sem->count = in_max;
    sem->max = in_max;
    keventInit(&(sem->wait_event), true);
    return KERNEL_RETURN_SUCCESS;
}

/** Signal (a.k.a. release or unlock) a semaphore. Should be called when a
  * thread has finished with a shared resource.
  * \param sem Semaphore to signal.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_TOO_MANY if the
  *         semaphore was signalled too many times.
  */
KernelReturn ksemSignal(KernelSemaphore *sem)
{
    enterCriticalSection();
    if (sem->count >= sem->max)
    {
        leaveCriticalSection();
        return KERNEL_RETURN_TOO_MANY; // too many signals
    }
    if (sem->count < 0)
    {
        // Semaphore count is negative, which means there is a thread
        // waiting on the semaphore.
        keventSet(&(sem->wait_event));
    }
    sem->count++;
    leaveCriticalSection();
    return KERNEL_RETURN_SUCCESS;
}

/** Wait for (a.k.a. acquire or lock) a semaphore. Should be called when a
  * thread is about to access a shared resource. Each call to this function
  * should be matched with a call to ksemSignal() when the thread is finished
  * with the resource.
  * \param sem Semaphore to wait on.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_TOO_MANY if
  *         there were too many threads already waiting on this semaphore.
  */
KernelReturn ksemWait(KernelSemaphore *sem)
{
    enterCriticalSection();
    if (sem->count == -127)
    {
        leaveCriticalSection();
        return KERNEL_RETURN_TOO_MANY; // too many waiting threads
    }
    sem->count--;
    if (sem->count < 0)
    {
        // Semaphore count is negative, which means this thread has to wait.
        keventWait(&(sem->wait_event));
    }
    leaveCriticalSection();
    return KERNEL_RETURN_SUCCESS;
}
