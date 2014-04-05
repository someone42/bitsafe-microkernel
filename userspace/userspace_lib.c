/** \file userspace_lib.c
  *
  * \brief Userspace system call library
  *
  * Functions in this file provide access to kernel functions through the
  * system call interface.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include "userspace_lib.h"
#include "../syscall.h"

/** Initialise an event. Do not call this on an active (i.e. in-use) event,
  * otherwise the event could end up in an undefined state.
  * Since the event structure resides in userspace memory, the event cannot
  * be used by threads in other processes.
  * \param event Pointer to an event structure to initialise.
  * \param auto_reset false = manual-reset event, true = auto-reset event.
  *                   An auto-reset event will reset itself after one thread
  *                   finishes waiting on it.
  */
void ueventInit(UserEvent *event, bool auto_reset)
{
    uint32_t r;
    supervisorCall(SYSCALL_EVENT_INIT, (uint32_t)event, (uint32_t)auto_reset, &r);
}

/** Causes the current thread to wait (block) on an event. The thread will
  * resume execution when the event is set (see ueventSet()).
  * Multiple threads can wait on the same event.
  * If the event is already set, this function will not block; it will return
  * immediately.
  * \param event Event to wait on. This should be a pointer to event structure
  *              previously initialised with ueventInit().
  */
void ueventWait(UserEvent *event)
{
    uint32_t r;
    supervisorCall(SYSCALL_EVENT_WAIT, (uint32_t)event, 0, &r);
}

/** Set an event, so that any waiting threads will be awakened (unblocked).
  * If there are multiple waiting threads, then what happens depends on
  * whether the event is an auto-reset event (see ueventInit()) or not.
  * - Setting an auto-reset event will only awaken one thread. This is because
  *   the event auto-resets after the first thread awakens.
  * - Setting a non-auto-reset event will awaken all waiting threads. This
  *   is because the event stays set after each thread awakens.
  * \param event Event to set. This should be a pointer to an event structure
  *              previously initialised with ueventInit().
  */
void ueventSet(UserEvent *event)
{
    uint32_t r;
    supervisorCall(SYSCALL_EVENT_SET, (uint32_t)event, 0, &r);
}

/** Reset an event, so that threads will have to wait (block) on it.
  * You do not need to call this function for
  * auto-reset events (see ueventInit()).
  * \param event Event to reset. This should be a pointer to an event
  *              structure previously initialised with ueventInit().
  */
void ueventReset(UserEvent *event)
{
    uint32_t r;
    supervisorCall(SYSCALL_EVENT_RESET, (uint32_t)event, 0, &r);
}

/** Initialise a semaphore. Do not call this on an active semaphore, otherwise
  * the semaphore could end up in an undefined state.
  * Since the semaphore structure resides in userspace memory, the semaphore
  * cannot be used by threads in other processes.
  * \param semaphore Pointer to a semaphore structure to initialise.
  * \param in_max Maximum semaphore count (also the initial count). To create
  *               a mutex/lock, pass 1 for this parameter.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM
  *         if in_max was invalid.
  */
KernelReturn usemInit(UserSemaphore *sem, int8_t in_max)
{
    uint32_t r;
    supervisorCall(SYSCALL_SEM_INIT, (uint32_t)sem, (uint32_t)in_max, &r);
    return (KernelReturn)r;
}

/** Signal (a.k.a. release or unlock) a semaphore. Should be called when a
  * thread has finished with a shared resource.
  * \param sem Semaphore to signal. This should be a pointer to a semaphore
  *            structure previously initialised with usemInit().
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_TOO_MANY if the
  *         semaphore was signalled too many times.
  */
KernelReturn usemSignal(UserSemaphore *sem)
{
    uint32_t r;
    supervisorCall(SYSCALL_SEM_SIGNAL, (uint32_t)sem, 0, &r);
    return (KernelReturn)r;
}

/** Wait for (a.k.a. acquire or lock) a semaphore. Should be called when a
  * thread is about to access a shared resource. Each call to this function
  * should be matched with a call to usemSignal() when the thread is finished
  * with the resource.
  * \param sem Semaphore to wait for. This should be a pointer to a semaphore
  *            structure previously initialised with usemInit().
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_TOO_MANY if
  *         there were too many threads already waiting on this semaphore.
  */
KernelReturn usemWait(UserSemaphore *sem)
{
    uint32_t r;
    supervisorCall(SYSCALL_SEM_WAIT, (uint32_t)sem, 0, &r);
    return (KernelReturn)r;
}

/** Create a new thread. New threads will begin execution immediately,
  * provided there are no higher-priority threads already running.
  * There is a global limit of #MAX_THREADS threads, and a per-process limit
  * of #THREADS_PER_PROCESS threads per process (see scheduler.c for these
  * definitions).
  * \param entry Entry function of the thread.
  * \param stack Initial stack pointer of the thread.
  * \param arg Argument for thread entry function (this can be null if the
  *            argument isn't used).
  * \param priority Thread priority (should be between #PRIORITY_LOWEST
  *                 and the current process' base priority).
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_GLOBAL_NO_FREE if
  *         there are too many total threads, #KERNEL_RETURN_PROCESS_NO_FREE if
  *         a given process has too many threads,
  *         and #KERNEL_RETURN_BAD_PRIORITY if the supplied priority is
  *         invalid.
  */
KernelReturn uschedCreateThread(void (*entry)(void *), uint32_t *stack, void *arg, uint8_t priority)
{
    uint32_t more_args[2];
    more_args[0] = (uint32_t)arg;
    more_args[1] = (uint32_t)priority;
    supervisorCall(SYSCALL_THREAD_CREATE, (uint32_t)entry, (uint32_t)stack, more_args);
    return (KernelReturn)(more_args[0]);
}

/** Exit from a thread, so that it won't be scheduled again. There is no way
  * to resurrect an exited threads. Exited threads do not count towards global
  * or per-process thread limits.
  */
void uschedExitThread(void)
{
    uint32_t r;
    supervisorCall(SYSCALL_THREAD_EXIT, 0, 0, &r);
}

/** Sleep the current thread for a set number of SysTick timer ticks. During
  * this time, the thread will not be scheduled, allowing other threads
  * to run. Use this function to relinquish CPU time to threads of equal or
  * lower priority.
  * \param ticks Number of SysTick timer ticks to sleep for. This must be
  *              at least #SLEEP_MINIMUM_TICKS (see scheduler.c). If you need
  *              to sleep for a smaller number of ticks, just use a delay
  *              loop.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         ticks was too small.
  * \warning Ticks within the scheduler aren't "counted" towards the sleep
  *          interval. What this means is that if the CPU is spending a lot of
  *          time within the scheduler, the actual (wallclock) sleep time may
  *          be significantly larger than the specified number of ticks. So
  *          you should not use this function for accurate timing. If you
  *          require accurate timing, a better idea would be to use a timer
  *          interrupt.
  */
KernelReturn uschedSleep(uint32_t ticks)
{
    uint32_t r;
    supervisorCall(SYSCALL_THREAD_SLEEP, ticks, 0, &r);
    return (KernelReturn)r;
}

#ifndef NO_PROCESSES

/** Claim ownership of a queue. This must be called before reading or writing
  * anything to the queue. There is a global open queue limit
  * of #MAX_NUM_QUEUES and a per-process open queue limit
  * of #QUEUES_PER_PROCESS (see queue.c for definitions). Because the number
  * of open queues is static and limited, you should call uqueueClose() as
  * soon as the queue is no longer needed.
  * \param dest_process_id The process ID of the destination (callee in an
  *                        inter-process call).
  * \param out_handle Upon success, the queue handle of the opened queue will
  *                   be written here.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_GLOBAL_NO_FREE
  *         if there are too many queues open
  *         across all processes, #KERNEL_RETURN_INVALID_PARAM if the
  *         supplied destination process ID is invalid,
  *         and #KERNEL_RETURN_PROCESS_NO_FREE if there are too many queues
  *         open for the current process.
  */
KernelReturn uqueueOpen(uint8_t dest_process_id, UserQueueHandle *out_handle)
{
    uint32_t r;
    supervisorCall(SYSCALL_QUEUE_OPEN, (uint32_t)dest_process_id, (uint32_t)out_handle, &r);
    return (KernelReturn)r;
}

/** Close a queue. This relinquishes ownership of the queue, so that it no
  * longer counts against any of the open queue limits.
  * \param handle Queue handle of queue to close. The queue must have been
  *               previously opened using uqueueOpen().
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_BAD_HANDLE if
  *         the supplied queue handle was invalid.
  */
KernelReturn uqueueClose(UserQueueHandle handle)
{
    uint32_t r;
    supervisorCall(SYSCALL_QUEUE_CLOSE, (uint32_t)handle, 0, &r);
    return (KernelReturn)r;
}

/** Write data to a queue. This data can be read from the other end using
  * uqueueRead(). This does enforce traffic directions: only the source
  * process (caller in an inter-process call) may write in the forwards
  * direction, and only the destination process (callee in an inter-process
  * call) may write in the backwards direction. This function will block
  * until exactly the specified number of bytes have been written to the
  * queue.
  * \param handle Queue handle of queue to write to. The queue must have been
  *               previously opened using uqueueOpen().
  * \param is_forward Specifies direction of write: true = forwards,
  *                   false = backwards.
  * \param data Data to write to the queue.
  * \param length Number of bytes to write to the queue.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_BAD_HANDLE if
  *         the supplied queue handle was invalid. This will also
  *         return #KERNEL_RETURN_BAD_HANDLE if there was a permissions error
  *         (eg. destination tried to write in forward direction); this is
  *         designed to make it difficult for processes to discover the
  *         queue handles of other processes.
  */
KernelReturn uqueueWrite(UserQueueHandle handle, bool is_forward, uint8_t *data, uint32_t length)
{
    uint32_t total_written;
    uint32_t written;
    KernelReturn r;
    uint32_t more_args[3];

    total_written = 0;
    while (total_written < length)
    {
        more_args[0] = (uint32_t)&(data[total_written]);
        more_args[1] = (uint32_t)(length - total_written);
        more_args[2] = (uint32_t)&written;
        supervisorCall(SYSCALL_QUEUE_WRITE, (uint32_t)handle, (uint32_t)is_forward, more_args);
        r = (KernelReturn)(more_args[0]);
        if (r == KERNEL_RETURN_SUCCESS)
        {
            total_written += written;
        }
        else
        {
            break;
        }
    }
    return r;
}

/** Read data from a queue. This reads the data that was written to the other
  * end using uqueueWrite(). This does enforce traffic directions: only the
  * destination process (callee in an inter-process call) may read in the
  * forwards direction, and only the source process (caller in an
  * inter-process call) may read in the backwards direction. This function
  * will block until exactly the specified number of bytes have been read from
  * the queue.
  * \param handle Queue handle of queue to read from. The queue must have been
  *               previously opened using uqueueOpen().
  * \param is_forward Specifies direction of read: true = forwards,
  *                   false = backwards.
  * \param data Buffer to store the bytes that were read from the queue.
  * \param size Number of bytes to read from the queue.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_BAD_HANDLE if
  *         the supplied queue handle was invalid. This will also
  *         return #KERNEL_RETURN_BAD_HANDLE if there was a permissions error
  *         (eg. destination tried to read in backwards direction); this is
  *         designed to make it difficult for processes to discover the
  *         queue handles of other processes.
  */
KernelReturn uqueueRead(UserQueueHandle handle, bool is_forward, uint8_t *data, uint32_t size)
{
    uint32_t total_read;
    uint32_t read;
    KernelReturn r;
    uint32_t more_args[3];

    total_read = 0;
    while (total_read < size)
    {
        more_args[0] = (uint32_t)&(data[total_read]);
        more_args[1] = (uint32_t)(size - total_read);
        more_args[2] = (uint32_t)&read;
        supervisorCall(SYSCALL_QUEUE_READ, (uint32_t)handle, (uint32_t)is_forward, more_args);
        r = (KernelReturn)(more_args[0]);
        if (r == KERNEL_RETURN_SUCCESS)
        {
            total_read += read;
        }
        else
        {
            break;
        }
    }
    return r;
}

/** Make an inter-process call. This will block until the call is handled.
  * If there are no IPC handlers at the destination, this will block until
  * one is available.
  * \param dest_process_id Process ID of call destination.
  * \param transfer_queue Queue handle, used to transfer parameters
  *                       and return values to and from the callee. The queue
  *                       must have been previously opened using uqueueOpen().
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the supplied destination process ID was invalid.
  */
KernelReturn uprocessCall(uint8_t dest_process_id, UserQueueHandle transfer_queue)
{
    uint32_t r;
    do
    {
        supervisorCall(SYSCALL_IPC_CALL, (uint32_t)dest_process_id, (uint32_t)transfer_queue, &r);
    } while (r == KERNEL_RETURN_WAIT);
    return (KernelReturn)r;
}

/** Wait for an incoming inter-process call. This will block until a call
  * to the current process is made.
  * \param source_process_id Upon success, the process ID of caller will be
  *                          written here.
  * \param transfer_queue Upon success, a queue handle (used to transfer
  *                       parameters and return values from and to the caller)
  *                       will be written here.
  */
void uprocessHandleCall(uint8_t *source_process_id, UserQueueHandle *transfer_queue)
{
    uint32_t r;
    do
    {
        supervisorCall(SYSCALL_IPC_HANDLE, (uint32_t)source_process_id, (uint32_t)transfer_queue, &r);
    } while (r == KERNEL_RETURN_WAIT);
}

/** This function will block the current thread until the specified interrupt
  * is triggered. Make sure the interrupt is enabled
  * first (see uinterruptEnable()).
  * \param irq IRQ number of interrupt to wait for.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the specified IRQ was invalid, and #KERNEL_RETURN_IRQ_PERMISSION
  *         if the current process doesn't have access to the specified IRQ.
  */
KernelReturn uinterruptWait(uint8_t irq)
{
    uint32_t r;
    supervisorCall(SYSCALL_INT_WAIT, (uint32_t)irq, 0, &r);
    return (KernelReturn)r;
}

/** Enable interrupt, so that it can be triggered. Interrupts are all disabled
  * by default.
  * \param irq IRQ number of interrupt to enable.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the specified IRQ was invalid, and #KERNEL_RETURN_IRQ_PERMISSION
  *         if the current process doesn't have access to the specified IRQ.
  */
KernelReturn uinterruptEnable(uint8_t irq)
{
    uint32_t r;
    supervisorCall(SYSCALL_INT_ENABLE, (uint32_t)irq, 0, &r);
    return (KernelReturn)r;
}

/** Disable interrupt, so that it will not be triggered.
  * \param irq IRQ number of interrupt to disable.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the specified IRQ was invalid, and #KERNEL_RETURN_IRQ_PERMISSION
  *         if the current process doesn't have access to the specified IRQ.
  */
KernelReturn uinterruptDisable(uint8_t irq)
{
    uint32_t r;
    supervisorCall(SYSCALL_INT_DISABLE, (uint32_t)irq, 0, &r);
    return (KernelReturn)r;
}

/** After an interrupt is triggered, it will not trigger again, until this
  * function is called. A function like this is necessary because some
  * interrupts will continuously fire when a condition is met (eg. FIFO empty).
  * Typically, a thread will call this function only after it has dealt with
  * the condition, in order to prevent a continuous stream of triggering.
  * \param irq IRQ number of interrupt to acknowledge.
  * \param is_level_sensitive Whether the interrupt is level-sensitive (true)
  *                           or pulse-sensitive (false). If a handler has to
  *                           clear some condition in a peripheral, then the
  *                           interrupt is probably level-sensitive. If in
  *                           doubt, assume the interrupt is pulse-sensitive.
  *                           If you then get spurious triggering, the
  *                           interrupt is actually level-sensitive.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the specified IRQ was invalid, and #KERNEL_RETURN_IRQ_PERMISSION
  *         if the current process doesn't have access to the specified IRQ.
  */
KernelReturn uinterruptAcknowledge(uint8_t irq, bool is_level_sensitive)
{
    uint32_t r;
    supervisorCall(SYSCALL_INT_ACKNOWLEDGE, (uint32_t)irq, (uint32_t)is_level_sensitive, &r);
    return (KernelReturn)r;
}

#endif // #ifndef NO_PROCESSES
