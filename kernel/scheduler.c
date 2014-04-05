/** \file scheduler.c
  *
  * \brief Kernel scheduler.
  *
  * The scheduler creates/destroys threads, and decides which thread to
  * context switch to.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "kernel.h"
#include "process_table_num.h"
#include "check_access.h"
#include "userspace/userspace_lib.h" // for ARRAY_AS_STACK

/** Maximum number of threads that can be created. Increasing this will
  * increase kernel memory usage, because this constant determines the
  * size of #thread_table. */
#define MAX_THREADS             32
/** Maximum number of threads a single process can create. This should be
  * smaller than #MAX_THREADS, so that an errant process can't hog all the
  * thread control blocks. */
#ifdef NO_PROCESSES
#define THREADS_PER_PROCESS     MAX_THREADS
#else // #ifdef NO_PROCESSES
#define THREADS_PER_PROCESS     12
#endif // #ifdef NO_PROCESSES
/** Minimum sleep delay, in ticks. This is set so that a single thread cannot
  * cause the scheduler to "thrash" (constantly resetting SysTick and
  * checking threads). */
#define SLEEP_MINIMUM_TICKS     16800

/** Placeholder for thread table indices (like #current_thread_index) which
  * specifies that there the index is invalid. */
#define INVALID_THREAD_INDEX    -1
/** Maximum value that can be loaded into the SysTick countdown. The
  * scheduler allows sleep delays bigger than this; such delays will be
  * accomplished through a series of SysTick countdowns. */
#define SYSTICK_MAX_LOAD        0x00ffffff

/** Possible thread states. Each #thread_table entry starts with a state
  * of #THREAD_STATE_NONE. */
enum ThreadState
{
    /** No thread in this thread table slot. */
    THREAD_STATE_NONE     = 0,
    /** Thread is currently running. Only one thread should have this state
      * at any one time. */
    THREAD_STATE_ACTIVE   = 1,
    /** Thread is waiting for an event. */
    THREAD_STATE_WAITING  = 2,
    /** Thread is about to run (waiting for scheduler to context switch to
      * it. */
    THREAD_STATE_PENDING  = 3,
    /** Thread is sleeping and will be awaken after some time delay. */
    THREAD_STATE_SLEEPING = 4
};

/** Information about the run state of a thread. Nothing in this structure
  * is valid unless the state field is something other
  * than #THREAD_STATE_NONE. Note that some stuff (registers, PC, etc.) are
  * stored on the thread's stack. */
typedef struct ThreadControlBlockStruct
{
    /** This is saved whenever we context switch away from a thread. */
    uint32_t *stack_pointer;
    /** If the thread is sleeping, (state is #THREAD_STATE_SLEEPING), this
      * is the number of ticks left until the thread will wake. */
    uint32_t ticks_to_wake;
    /** Current state of the thread; should be one of #ThreadState. If this
      * is #THREAD_STATE_NONE, then none of the other fields are valid. */
    uint8_t state;
    /** Parent process of this thread. For security reasons, the thread has
      * no control over this. */
    uint8_t process;
    /** If the thread is waiting (state is #THREAD_STATE_WAITING), this
      * specifies the IRQ number the thread is waiting for. */
    uint8_t waiting_irq;
    /** Priority of the thread. For security reasons, the thread has limited
      * control over this. */
    uint8_t priority;
    /** If the thread is waiting for an event, this specifies which event
      * the thread is waiting on. */
    KernelEvent *waiting_event;
} ThreadControlBlock;

/** Index into #thread_table of current thread. This is provided for the
  * convenience of the C-language portion of the context switcher. */
static int current_thread_index;

/** Array of thread control blocks. Every thread (that the scheduler knows
  * about) will have an entry in this table. This approach probably doesn't
  * scale well (a binary heap arranged by priority would be more efficient
  * for the scheduler), but it's simple. */
static ThreadControlBlock thread_table[MAX_THREADS];

/** This is set to true whenever something needs to happen during thread
  * switch. */
static volatile bool switch_thread_call;
/** This is set to true when scheduler starts up. It is used to ensure that
  * the unprivileged mode switch happens during a context switch (which is
  * a safe time to do the switch). */
static volatile bool starting_scheduler;
/** Whether scheduler has started yet. */
static volatile bool scheduler_started;
/** This is set to true when a thread wants to exit. */
static volatile bool thread_wants_to_exit;
/** Index into #thread_table of thread that wants to exit. */
static int thread_exit_index;

/** Number of threads for each process. */
uint8_t process_thread_counter[NUM_PROCESSES];

/** Place to park PSP after scheduler starts. This is required because
  * there is a brief moment before the first thread is scheduled when an
  * interrupt can occur while the process stack is being used.
  * This is large enough to handle at least one exception frame. */
static uint8_t psp_park_location[80];

/** Request a context switch. The context switch won't be done immediately;
  * it will be serviced by the PendSV exception after every other handler
  * has finished.
  * It's okay to call this multiple times before an actual context switch.
  * The PendSV handler will decide which thread to switch to if there are
  * multiple pending threads. */
static void kschedRequestContextSwitch(void)
{
    // Request PendSV. The PendSV handler will take it from there.
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    __DSB();
    __ISB();
}

/** This implements the effect of SysTick timer ticks passing.
  * Because this checks the state of every thread (to see whether it should
  * be awaken), this should be called as infrequently as possible - ideally,
  * only every time a thread would need to be awaken.
  * \param ticks SysTick timer ticks which passed.
  */
static void processTicks(uint32_t ticks)
{
    int i;

    enterCriticalSection();

    // Decrement waiting ticks, possibly waking up some threads.
    for (i = 0; i < MAX_THREADS; i++)
    {
        if (thread_table[i].state == THREAD_STATE_SLEEPING)
        {
            if (thread_table[i].ticks_to_wake <= ticks)
            {
                // Thread sleep timer has expired; wake up thread.
                thread_table[i].ticks_to_wake = 0;
                thread_table[i].state = THREAD_STATE_PENDING;
                kschedRequestContextSwitch();
            }
            else
            {
                thread_table[i].ticks_to_wake -= ticks;
            }
        }
    }

    // Calculate next interval to count down from.
    ticks = SYSTICK_MAX_LOAD;
    for (i = 0; i < MAX_THREADS; i++)
    {
        if (thread_table[i].state == THREAD_STATE_SLEEPING)
        {
            if (thread_table[i].ticks_to_wake < ticks)
            {
                ticks = thread_table[i].ticks_to_wake;
            }
        }
    }
    if (ticks < 1)
    {
        // The minimum value for LOAD is 1, otherwise the SysTick interrupt
        // won't fire.
        ticks = 1;
    }

    // Program SysTick timer for next interval.
    SysTick->LOAD = ticks;
    SysTick->VAL = 0; // clear current value
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; // enable counter
    leaveCriticalSection();
}

/** SysTick timer timeout interrupt handler. */
void SysTick_Handler(void)
{
    enterCriticalSection();
    // Sometimes, if the timeout ends up being very small (this can happen
    // if two threads sleep for amounts which are almost the same), the SysTick
    // interrupt can end up firing again during handling. This can cause
    // the handler to be spuriously entered twice. So we introduce a COUNTFLAG
    // check here to ignore those spurious interrupts.
    if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0)
    {
        SysTick->CTRL &= (~SysTick_CTRL_ENABLE_Msk); // disable counter
        __DSB();
        // Reset COUNTFLAG so that the scheduler can determine if a SysTick
        // interrupt was supposed to occur, but didn't because interrupts were
        // disabled.
        SysTick->CTRL &= (~SysTick_CTRL_COUNTFLAG_Msk);
        // No-one messes with LOAD while SysTick is running, so it is safe to
        // assume LOAD ticks have passed.
        processTicks(SysTick->LOAD);
    }
    leaveCriticalSection();
}

/** This is called whenever #switch_thread_call is set to true. This
  * function does deferred processing that should only be done during a
  * context switch (eg. transitioning to unprivileged mode). */
static void kschedSwitchThreadCall(void)
{
    if (starting_scheduler)
    {
        // Set bits 0 and 1 of CONTROL register. This sets PSP as the
        // Thread mode stack pointer. It also sets Thread mode privilege
        // level to unprivileged.
        __set_CONTROL(0x00000003);
        starting_scheduler = false;
    }
    if (thread_wants_to_exit)
    {
        // Need to kill a thread as well. Clear its TCB so it will never
        // be scheduled.
        KERNEL_ASSERT(thread_exit_index != INVALID_THREAD_INDEX);
        memset(&(thread_table[thread_exit_index]), 0, sizeof(ThreadControlBlock));
        thread_table[thread_exit_index].state = THREAD_STATE_NONE;
        if (thread_table[thread_exit_index].process < NUM_PROCESSES)
        {
            process_thread_counter[thread_table[thread_exit_index].process]--;
        }
        thread_wants_to_exit = false;
        thread_exit_index = INVALID_THREAD_INDEX; // park at invalid value
    }
}

/** The C-language portion of the PendSV interrupt handler. This handles
  * the "complicated" stuff, like deciding which thread to schedule next.
  * This should only be called by the assembly-language portion PendSV handler!
  * \param old_stack_pointer Stack pointer (after saving state) of thread
  *                          that we're switching from.
  * \return Stack pointer (before restoring state) of thread that we're
  *         switching to.
  */
uint32_t *kschedSwitchThread(uint32_t *old_stack_pointer)
{
    int i;
    bool thread_found;
    unsigned int highest_priority;
    int highest_priority_index;

    enterCriticalSection();

    if (current_thread_index != INVALID_THREAD_INDEX)
    {
        if (thread_table[current_thread_index].state == THREAD_STATE_ACTIVE)
        {
            // Mark current thread as pending, so that no threads are active.
            thread_table[current_thread_index].state = THREAD_STATE_PENDING;
        }
        thread_table[current_thread_index].stack_pointer = old_stack_pointer;
    }

    if (switch_thread_call)
    {
        kschedSwitchThreadCall();
        switch_thread_call = false;
    }

    // Find the thread with the highest priority to schedule.
    thread_found = false;
    highest_priority_index = INVALID_THREAD_INDEX;
    highest_priority = PRIORITY_IDLE + 1; // "+ 1" so test below picks up idle thread
    for (i = 0; i < MAX_THREADS; i++)
    {
        if ((thread_table[i].state == THREAD_STATE_ACTIVE)
            || (thread_table[i].state == THREAD_STATE_PENDING))
        {
            if (thread_table[i].priority < highest_priority)
            {
                highest_priority = thread_table[i].priority;
                highest_priority_index = i;
                thread_found = true;
            }
        }
    }
    KERNEL_ASSERT(thread_found); // should never fail because idle thread is always there

    current_thread_index = highest_priority_index;
    thread_table[current_thread_index].state = THREAD_STATE_ACTIVE;
    kprocessSwitchProcess(thread_table[current_thread_index].process);

    leaveCriticalSection();

    return thread_table[current_thread_index].stack_pointer;
    // The assembly-language portion of the PendSV handler will now restore
    // the context of the thread associated with current_thread_index.
}

/** Exit from a thread, so that it won't be scheduled again. */
void kschedExitThread(void)
{
    enterCriticalSection();
    // Can't destroy thread here; have to context switch away from it first,
    // so schedule a context switch and get the context switcher to destroy
    // the thread.
    thread_wants_to_exit = true;
    thread_exit_index = current_thread_index;
    switch_thread_call = true;
    kschedRequestContextSwitch();
    leaveCriticalSection();
}

/** Create a new thread. New threads will begin execution immediately,
  * provided there are no higher-priority threads already running.
  * Up to #MAX_THREADS threads can be created.
  * \param entry Entry function of the thread.
  * \param stack Initial stack pointer of the thread.
  * \param arg Argument for thread entry function.
  * \param priority Thread priority (should be between #PRIORITY_LOWEST
  *                 and the current process' base priority).
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_GLOBAL_NO_FREE if
  *         there are too many total threads, #KERNEL_RETURN_PROCESS_NO_FREE if
  *         a given process has too many threads,
  *         and #KERNEL_RETURN_BAD_PRIORITY if the supplied priority is
  *         invalid.
  */
KernelReturn kschedCreateThread(void (*entry)(void *), uint32_t *stack, void *arg, uint8_t priority)
{
    int i, j;
    uint8_t current_process_id;

    enterCriticalSection();

    // Don't allow the thread priority to be any higher than the process' base
    // priority.
    if ((priority < kprocessGetCurrentBasePriority())
        || (priority > PRIORITY_IDLE)
        || (priority < PRIORITY_HIGHEST))
    {
        leaveCriticalSection();
        return KERNEL_RETURN_BAD_PRIORITY;
    }
    // Check per-process thread count doesn't exceed limit.
    current_process_id = kprocessGetCurrent();
    KERNEL_ASSERT(current_process_id < NUM_PROCESSES);
    if (process_thread_counter[current_process_id] >= THREADS_PER_PROCESS)
    {
        leaveCriticalSection();
        return KERNEL_RETURN_PROCESS_NO_FREE;
    }

    // Find free entry in thread table.
    for (i = 0; i < MAX_THREADS; i++)
    {
        if (thread_table[i].state == THREAD_STATE_NONE)
        {
            // Claim entry.
            memset(&(thread_table[i]), 0, sizeof(ThreadControlBlock));
            thread_table[i].state = THREAD_STATE_PENDING;

            // Make the thread's stack look like it is in the middle of an
            // interrupt. The scheduler will use a return from PendSV (using the
            // thread's stack) to activate the thread.
            storeCheckedWord(--stack, 0x01000000); // xPSR (Thumb bit must be set or HardFault will occur)
            storeCheckedWord(--stack, (uint32_t)entry); // PC
            // Pointing LR at kschedExitThread() won't work when the MPU is
            // enabled, because kschedExitThread() sits in kernelspace, and is
            // inaccessible to userspace. So returning will cause a memory
            // management fault. Oh well, if a thread wants to exit, it should
            // explicitly call the SYSCALL_THREAD_EXIT syscall.
            storeCheckedWord(--stack, (uint32_t)kschedExitThread); // LR
            for (j = 0; j < 4; j++)
            {
                storeCheckedWord(--stack, 0); // R12, R3, R2 and R1
            }
            storeCheckedWord(--stack, (uint32_t)arg); // R0
            storeCheckedWord(--stack, 0xfffffffd); // LR (return to Thread mode, use PSP, no floating-point)
            for (j = 0; j < 8; j++)
            {
                storeCheckedWord(--stack, 0); // R4, R5, R6, R7, R8, R9, R10, R11
            }
            thread_table[i].stack_pointer = stack;
            thread_table[i].process = current_process_id;
            thread_table[i].priority = priority;
            process_thread_counter[current_process_id]++;
            // Only request a context switch if the scheduler has actually
            // started (i.e. kschedStart() has been called). Otherwise
            // the scheduler will try to context switch away when PSP
            // is still undefined.
            if (scheduler_started)
            {
                kschedRequestContextSwitch();
            }

            leaveCriticalSection();
            return KERNEL_RETURN_SUCCESS;
        }
    }

    leaveCriticalSection();
    return KERNEL_RETURN_GLOBAL_NO_FREE; // no free entries in thread table
}

/** Sleep the current thread for a set number of SysTick timer ticks. During
  * this time, the thread will not be scheduled, allowing other threads
  * to run.
  * \param ticks Number of SysTick timer ticks to sleep for. This must be
  *              at least #SLEEP_MINIMUM_TICKS. If you need to sleep for
  *              a smaller number of ticks, just use a delay loop.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         ticks was too small.
  */
KernelReturn kschedSleep(uint32_t ticks)
{
    uint32_t val;

    // Enforce minimum delay, so that a single thread cannot cause the
    // scheduler to thrash.
    if (ticks < SLEEP_MINIMUM_TICKS)
    {
        return KERNEL_RETURN_INVALID_PARAM; // invalid ticks value
    }
    enterCriticalSection();
    KERNEL_ASSERT(current_thread_index != INVALID_THREAD_INDEX);
    thread_table[current_thread_index].state = THREAD_STATE_SLEEPING;
    thread_table[current_thread_index].ticks_to_wake = ticks;
    // Interrupt current SysTick countdown, because the thread's sleep timeout
    // may be smaller than the SysTick countdown.
    // Interrupts are disabled (because we're in a critical section), but
    // SysTick will continue to tick down. There is the possibility that
    // the countdown has ended inside this critical section (so VAL will
    // have wrapped around). Fourtunately, the COUNTFLAG bit will be set
    // if the countdown ends; in that case we just do nothing and let
    // the SysTick handler do all the work.
    val = SysTick->VAL;
    __DSB();
    if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0)
    {
        // VAL is guaranteed to not have wrapped around since it is read
        // before COUNTFLAG.
        SysTick->CTRL &= (~SysTick_CTRL_ENABLE_Msk); // disable counter
        // Elapsed ticks are the initial load value (LOAD) - current
        // countdown value.
        processTicks(SysTick->LOAD - val);
    }
    kschedRequestContextSwitch();
    leaveCriticalSection();
    return KERNEL_RETURN_SUCCESS; // success
}

/** Get current thread to wait on a kernel event.
  * \param event Kernel event to wait on.
  */
void kschedWait(KernelEvent *event)
{
    enterCriticalSection();
    KERNEL_ASSERT(current_thread_index != INVALID_THREAD_INDEX);
    thread_table[current_thread_index].state = THREAD_STATE_WAITING;
    thread_table[current_thread_index].waiting_event = event;
    kschedRequestContextSwitch();
    leaveCriticalSection();
}

/** Awaken threads which are waiting on a kernel event. If multiple threads
  * are waiting on a single kernel event, the number of threads awakened is
  * controlled by the all parameter.
  * \param event Kernel event to awaken thread by.
  * \param all false means that at most one thread will be awakened,
  *            true means that all waiting threads will be awakened.
  * \return Number of threads waiting on the kernel event (before any
  *         awakening). The caller can use this to determine if there are
  *         any other waiting threads.
  */
uint32_t kschedNotify(KernelEvent *event, bool all)
{
    uint32_t num_waiting_threads;
    int i;
    bool found;

    enterCriticalSection();

    // Count and awaken waiting threads.
    num_waiting_threads = 0;
    found = false;
    for (i = 0; i < MAX_THREADS; i++)
    {
        if ((thread_table[i].state == THREAD_STATE_WAITING)
            && (thread_table[i].waiting_event == event))
        {
            if (!found || all)
            {
                // Wake up this thread.
                thread_table[i].state = THREAD_STATE_PENDING;
                kschedRequestContextSwitch();
                found = true; // if all = false, this will suppress all further awakenings
            }
            num_waiting_threads++;
        }
    }

    leaveCriticalSection();
    return num_waiting_threads;
}

/** Initialise and start scheduler. */
void kschedStart(void)
{
    enterCriticalSection();

    // Set up NVIC so that all 4 priority bits are for pre-emption priority;
    // none are allocated for sub-priority.
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    // SysTick is used to schedule wakeups for sleeping threads. To properly
    // handle the wakeup of threads with high priority, the SysTick
    // interrupt must have the highest priority.
    NVIC_SetPriority(SysTick_IRQn, 0x00);
    // PendSV is used to context switch. We only want the context switch
    // to take place when all pending interrupt handlers have finished (so that
    // the correct context switch happens), so PendSV should have the lowest
    // priority.
    NVIC_SetPriority(PendSV_IRQn, 0xff);
    // Set up SysTick timer to begin ticking aimlessly, because the rest of
    // the scheduler assumes that it's running.
    SysTick->CTRL = 0; // disable counter
    SysTick->CTRL = SysTick_CTRL_TICKINT_Msk| SysTick_CTRL_CLKSOURCE_Msk; // clock source = AHB, use interrupt
    SysTick->LOAD = SYSTICK_MAX_LOAD;
    SysTick->VAL = 0; // clear current value
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; // enable counter

    current_thread_index = INVALID_THREAD_INDEX;

    __set_PSP((uint32_t)ARRAY_AS_STACK(psp_park_location));
    starting_scheduler = true;
    switch_thread_call = true;
    scheduler_started = true;
    kschedRequestContextSwitch();

    leaveCriticalSection();
}
