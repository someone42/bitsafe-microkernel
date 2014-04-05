/** \file process.c
  *
  * \brief Manages processes and inter-process calls.
  *
  * Processes are statically-defined, so many of the functions in this file are
  * basically a driver for the STM32 Cortex-M4F MPU.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"
#include "stm32f4xx.h"

/** Placeholder for #current_process_id which indicates that the process ID
  * is invalid. */
#define INVALID_PROCESS_ID      0xff

/** Number of memory regions supported by STM32 Cortex-M4F MPU. */
#define MPU_NUM_REGIONS         8

/** Inter-process call (IPC) structure which deals with the fact that there
  * can be multiple simultaneous callers and handlers. It's basically a
  * FIFO with one entry. */
typedef struct IPCQueueStruct
{
    /** Whether there is an outstanding (unhandled) IPC. */
    bool has_call;
    /** Process ID of caller. Only valid if has_call is true. */
    uint8_t source_process_id;
    /** Caller's transfer queue. Only valid if has_call is true. */
    QueueHandle transfer_queue;
    /** Set when not empty, reset when empty. */
    KernelEvent not_empty_event;
    /** Set when not full, reset when empty. */
    KernelEvent not_full_event;
} IPCQueue;

// Include auto-generated table of process properties.
#include "process_table.h"

/** Inter-process call queues to co-ordinate callers and handlers for each
  * process. */
static IPCQueue ipc_queues[NUM_PROCESSES];

/** Process IDs of owners of each IRQ number. */
static uint8_t irq_owners[NUM_IRQ];

/** Process ID of current active thread. */
static uint8_t current_process_id = INVALID_PROCESS_ID;

/** Initialise process switcher. Call this before starting scheduler. */
void kprocessInit(void)
{
    int i;
    int j;
    uint8_t irq;

    enterCriticalSection();

    // Sanity check - check DREGION field for MPU TYPE register matches
    // expected number of regions.
    KERNEL_ASSERT(((MPU->TYPE >> 8) & 0xff) == MPU_NUM_REGIONS);

    // Enable Cortex-M4F MPU, use default map in privileged mode.
    MPU->CTRL = 0x00000005;
    __DSB();
    __ISB();

    // Initialise IPC queues.
    for (i = 0; i < NUM_PROCESSES; i++)
    {
        ipc_queues[i].has_call = false;
        keventInit(&(ipc_queues[i].not_empty_event), false);
        keventInit(&(ipc_queues[i].not_full_event), false);
    }

    // Assign IRQ owners.
    for (i = 0; i < NUM_IRQ; i++)
    {
        irq_owners[i] = INVALID_PROCESS_ID;
    }
    for (i = 0; i < NUM_PROCESSES; i++)
    {
        for (j = 0; j < MAX_IRQ_HANDLERS; j++)
        {
            irq = processes[i].irq_handlers[j];
            // The default initialiser is 0, so interpret 0 as "nothing".
            if (irq != 0)
            {
                // Since 0 is reserved for "nothing", shift number by 1 so
                // that it is still possible to represent IRQ 0.
                irq--;
                KERNEL_ASSERT(irq < NUM_IRQ);
                KERNEL_ASSERT(irq_owners[irq] == INVALID_PROCESS_ID); // already claimed?
                irq_owners[irq] = (uint8_t)i;
            }
        }
    }

    leaveCriticalSection();
}

/** Get process ID of current active thread.
  * \return Process ID.
  */
uint8_t kprocessGetCurrent(void)
{
    uint8_t cached_current_process_id;

    cached_current_process_id = current_process_id; // to avoid race condition
    KERNEL_ASSERT(cached_current_process_id != INVALID_PROCESS_ID);
    KERNEL_ASSERT(cached_current_process_id < NUM_PROCESSES);
    return cached_current_process_id;
}

/** Switch to another process' address space.
  * This should only be called by the scheduler.
  * \param new_process_id Process ID of process to switch to.
  */
void kprocessSwitchProcess(uint8_t new_process_id)
{
    unsigned int j;
    unsigned int num_regions;

    enterCriticalSection();
    KERNEL_ASSERT(new_process_id < NUM_PROCESSES);
    if (current_process_id != new_process_id)
    {
        // Need to reload MPU registers.
        // This loop relies on the VALID bit and REGION fields being set
        // correctly in the RBAR register for each region. This avoids
        // having to write to MPU->RNR for each region.
        num_regions = processes[new_process_id].num_regions;
        KERNEL_ASSERT(num_regions <= MPU_NUM_REGIONS);
        for (j = 0; j < num_regions; j++)
        {
            MPU->RBAR = processes[new_process_id].regions[j].rbar;
            MPU->RASR = processes[new_process_id].regions[j].rasr;
        }
        __DSB();
        __ISB();
        current_process_id = new_process_id;
    }
    leaveCriticalSection();
}

/** Make an inter-process call. This will block until the call is handled.
  * If there are no IPC handlers at the destination, this will block until
  * one is available.
  * \param dest_process_id Process ID of call destination.
  * \param transfer_queue Kernel queue handle, used to transfer parameters
  *                       and return values to the callee.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the supplied destination process ID was invalid,
  *         and #KERNEL_RETURN_WAIT if the function has finished waiting (so
  *         the thread should call this function again with exactly the same
  *         parameters).
  */
KernelReturn kprocessCall(uint8_t dest_process_id, QueueHandle transfer_queue)
{
    if (dest_process_id >= NUM_PROCESSES)
    {
        return KERNEL_RETURN_INVALID_PARAM;
    }

    enterCriticalSection();
    if (!ipc_queues[dest_process_id].has_call)
    {
        // Write entry to the "queue".
        ipc_queues[dest_process_id].has_call = true;
        ipc_queues[dest_process_id].source_process_id = kprocessGetCurrent();
        ipc_queues[dest_process_id].transfer_queue = transfer_queue;
        keventSet(&(ipc_queues[dest_process_id].not_empty_event));
        keventReset(&(ipc_queues[dest_process_id].not_full_event));
        // Wait until someone handles the call.
        keventWait(&(ipc_queues[dest_process_id].not_full_event));
        leaveCriticalSection();
        return KERNEL_RETURN_SUCCESS;
    }
    else
    {
        // IPC "queue" is full, so wait until it isn't.
        keventWait(&(ipc_queues[dest_process_id].not_full_event));
        leaveCriticalSection();
        return KERNEL_RETURN_WAIT;
    }
}

/** Wait for an incoming inter-process call. This will block until a call
  * to the current process is made.
  * \param source_process_id Upon success, the process ID of caller will be
  *                          written here.
  * \param transfer_queue Upon success, a kernel queue handle (used to transfer
  *                       parameters and return values to the caller) will be
  *                       written here.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_WAIT if the
  *         function has finished waiting (so the thread should call this
  *         function again with exactly the same parameters).
  */
KernelReturn kprocessHandleCall(uint8_t *source_process_id, QueueHandle *transfer_queue)
{
    uint8_t my_process_id;

    enterCriticalSection();
    my_process_id = kprocessGetCurrent();

    if (ipc_queues[my_process_id].has_call)
    {
        // Read entry from the "queue".
        ipc_queues[my_process_id].has_call = false;
        *source_process_id = ipc_queues[my_process_id].source_process_id;
        *transfer_queue = ipc_queues[my_process_id].transfer_queue;
        keventReset(&(ipc_queues[my_process_id].not_empty_event));
        keventSet(&(ipc_queues[my_process_id].not_full_event));
        leaveCriticalSection();
        return KERNEL_RETURN_SUCCESS;
    }
    else
    {
        // No outstanding calls to service. Need to wait until there is one.
        keventWait(&(ipc_queues[my_process_id].not_empty_event));
        leaveCriticalSection();
        return KERNEL_RETURN_WAIT;
    }
}

/** Check whether current process owns a given IRQ. This should be used to
  * ensure that processes only wait on a specific set of IRQs.
  * \param irq IRQ number to check.
  * \return true if the current process does own the IRQ, false if not.
  */
bool kprocessIsCurrentProcessIRQOwner(uint8_t irq)
{
    if (irq >= NUM_IRQ)
    {
        return false;
    }
    else
    {
        if (irq_owners[irq] == kprocessGetCurrent())
        {
            return true;
        }
        else
        {
            return false;
        }
    }
}

/** The base priority of a process is the highest priority that any of its
  * threads can have. This is a security measure: errant processes cannot
  * pre-empt higher-priority processes, since the base priority is
  * statically defined.
  * \return Maximum priority of current process.
  */
uint8_t kprocessGetCurrentBasePriority(void)
{
    uint8_t process_id;

    process_id = kprocessGetCurrent();
    KERNEL_ASSERT(process_id < NUM_PROCESSES);
    return processes[process_id].priority;
}
