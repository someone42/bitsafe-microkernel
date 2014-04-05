/** \file examples.c
  *
  * \brief Examples of kernel function call use.
  *
  * Nothing here is part of the kernel or userspace library. These are just
  * examples. You can change everything in here without impacting the
  * functionality of the kernel.
  *
  * This is written for the STM32F4 Discovery Kit. The following things
  * should be observed:
  * - The green LED flashes reguarly (auto-reset events test)
  * - The blue LED flashes erratically (manual-reset events test)
  * - The red LED flashes in a repeating pattern (semaphores test)
  * - The orange LED flickers very rapidly (FIFO test which involves events and
  *   semaphores)
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_tim.h"
#include "misc.h"
#include "userspace/userspace_lib.h"

/** Size of thread-safe FIFO storage, in number of items. */
#define FIFO_SIZE       16

/** Thread-safe FIFO. Use fifoGet() and fifoPut() to access this. */
struct FIFO
{
    uint8_t storage[FIFO_SIZE]; /**< Storage for FIFO items. */
    volatile unsigned int available; /**< Number of available items. */
    volatile unsigned int next; /**< Index of next item to get. */
    UserSemaphore mutex; /**< Lock used to protect changes to FIFO. */
    UserEvent not_empty; /**< Set if storage is not empty. Manual reset. */
    UserEvent not_full; /**< Set if storage is not full. Manual reset. */
};

// Stacks for the various threads. The easiest way to reserve stack space for
// a thread is to declare a uint8_t array and use the ARRAY_AS_STACK macro
// to pass it to uschedCreateThread().
// Threads should have a minimum stack space of 64 bytes, for the context
// switcher.
static uint8_t event_set_thread_stack[256];
static uint8_t event_wait_thread_stack[256];
static uint8_t event_wait_thread_2_stack[256];
static uint8_t event_set_thread_2_stack[256];
static uint8_t event_set_thread_3_stack[256];
static uint8_t sem_thread_1_stack[256];
static uint8_t sem_thread_2_stack[256];
static uint8_t sem_thread_3_stack[256];
static uint8_t fifo_write_thread_stack[256];
static uint8_t fifo_read_thread_stack[256];

// Objects shared between threads.
static UserEvent event_autoreset;
static UserEvent event_autoreset_junk;
static UserEvent event_manualreset;
static UserSemaphore demo_sem;
static struct FIFO demo_fifo;

// Function prototypes, because startupThread() forward-references them.
static void eventSetThread(void *arg);
static void eventSetAndResetThread(void *arg);
static void eventWaitGreenThread(void *arg);
static void eventWaitBlueThread(void *arg);
static void semFlashRedThread(void *arg);
static void fifoInit(struct FIFO *fifo);
static void fifoWriteThread(void *arg);
static void fifoReadThread(void *arg);

/** Userspace entry point.
  *
  * This is the first thread that will be scheduled. Startup tasks should be
  * done in this thread. Since the startup thread has the highest priority but
  * limited stack space, you should only perform startup functions and then
  * call uschedExitThread() as soon as possible. */
void startupThread(void *arg)
{
    GPIO_InitTypeDef  GPIO_InitStructure;

    // GPIOD Periph clock enable.
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

    // Configure PD12, PD13, PD14 and PD15 (which are connected to LEDs on
    // the STM32F4 Discovery Kit) in output pushpull mode.
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // Events and semaphores must be initialised before being used.
    // The "true" below means that demo_event_autoreset will be an auto-reset
    // event. That means it will reset itself after a thread finishes waiting
    // on the event.
    ueventInit(&event_autoreset, true);
    ueventInit(&event_autoreset_junk, true);
    // The "false" below means that the event will remain set until it is
    // explicitly reset.
    ueventInit(&event_manualreset, false);
    // The "1" below means the semaphore has a maximum count of 1. That means
    // that at most one thread can acquire the semaphore (i.e. it is
    // equivalent to a mutex).
    usemInit(&demo_sem, 1);

    // The third argument in each uschedCreateThread() call will be passed as
    // "arg" to the thread function. It can be NULL if the thread function
    // doesn't use arg.
    // Lower priority numbers mean higher priority. So "PRIORITY_LOWEST - 1"
    // means 1 priority level higher than the lowest.
    uschedCreateThread(eventSetThread, ARRAY_AS_STACK(event_set_thread_stack), (void *)&event_autoreset, PRIORITY_LOWEST - 1);
    uschedCreateThread(eventWaitGreenThread, ARRAY_AS_STACK(event_wait_thread_stack), (void *)&event_autoreset, PRIORITY_LOWEST - 1);
    // The following call to uschedCreateThread() demonstrates that it is
    // possible to create a thread which calls the same thread function,
    // as long as the thread has its own stack.
    // The thread argument (arg) is set to demo_event_autoreset_junk, so
    // that event will periodically be set by eventSetThread().
    uschedCreateThread(eventSetThread, ARRAY_AS_STACK(event_set_thread_2_stack), (void *)&event_autoreset_junk, PRIORITY_LOWEST - 1);

    // The next two threads created below demonstrate manual reset events.
    // eventSetAndResetThread() will periodically set and reset
    // event_manualreset.
    // eventWaitBlueThread() will toggle the blue LED whenever
    // event_manualreset is set. This will cause the the blue LED to flash
    // erratically. Whenever event_manualreset is reset, the blue LED will
    // pause in whatever state (on or off) it was in the moment before reset.
    // However, whenever event_manualreset is set, the blue LED will toggle
    // on and off very fast (it will appear to be on with less brightness),
    // since event_manualreset remains set.
    // Thread priority is set at "lowest" so that the continuous toggling won't
    // starve other threads of CPU time.
    uschedCreateThread(eventSetAndResetThread, ARRAY_AS_STACK(event_set_thread_3_stack), (void *)&event_manualreset, PRIORITY_LOWEST);
    uschedCreateThread(eventWaitBlueThread, ARRAY_AS_STACK(event_wait_thread_2_stack), (void *)&event_manualreset, PRIORITY_LOWEST);

    // Create three threads which semaphore acquire intervals of 50 ms, 200 ms
    // and 5 s. These threads will fight for the acquisition of demo_sem.
    // The effect is that whenever the red LED is on, other threads will not
    // interrupt the flashing pattern.
    // For example, every 10 s the third thread (i.e. the one with
    // sem_thread_3_stack as the stack) will acquire demo_sem. The red LED will
    // then remain continuously on (i.e. uninterrupted) for 5 s. After those
    // 5 s, the third thread will release demo_sem, allowing the other two
    // threads to gain control of the red LED.
    uschedCreateThread(semFlashRedThread, ARRAY_AS_STACK(sem_thread_1_stack), (void *)8400000, PRIORITY_LOWEST - 1);
    uschedCreateThread(semFlashRedThread, ARRAY_AS_STACK(sem_thread_2_stack), (void *)33600000, PRIORITY_LOWEST - 1);
    uschedCreateThread(semFlashRedThread, ARRAY_AS_STACK(sem_thread_3_stack), (void *)840000000, PRIORITY_LOWEST - 1);

    // All of the examples given so far have been a bit artificial. The
    // next two threads demonstrate a more realistic scenario: a
    // producer-consumer problem. Here, a write thread writes sequential
    // values to demo_fifo (a blocking FIFO), while the read thread reads
    // values from demo_fifo and verifies that they are indeed sequential.
    fifoInit(&demo_fifo);
    srand(42);
    uschedCreateThread(fifoWriteThread, ARRAY_AS_STACK(fifo_write_thread_stack), (void *)&demo_fifo, PRIORITY_LOWEST - 1);
    uschedCreateThread(fifoReadThread, ARRAY_AS_STACK(fifo_read_thread_stack), (void *)&demo_fifo, PRIORITY_LOWEST - 1);

    // This will kill the startup thread. Note that uschedExitThread() doesn't
    // ever return.
    uschedExitThread();
}

/** Thread which periodically sets an event.
  * \param arg The event to set.
  */
static void eventSetThread(void *arg)
{
    while(true)
    {
        ueventSet((UserEvent *)arg);
        uschedSleep(16800000); // 100 millisecond
    }
}

/** Thread which periodically sets and reset an event.
  * \param arg The event to set and reset.
  */
static void eventSetAndResetThread(void *arg)
{
    while(true)
    {
        ueventSet((UserEvent *)arg);
        uschedSleep(25200000); // 150 millisecond
        ueventReset((UserEvent *)arg);
        uschedSleep(84000000); // 500 millisecond
    }
}

/** Thread which waits for an event and blinks green in response.
  * \param arg The event to wait for.
  */
static void eventWaitGreenThread(void *arg)
{
    while(true)
    {
        ueventWait((UserEvent *)arg);
        GPIO_ToggleBits(GPIOD, GPIO_Pin_12); // blink green LED
    }
}

/** Thread which waits for an event and blinks blue in response.
  * \param arg The event to wait for.
  */
static void eventWaitBlueThread(void *arg)
{
    while(true)
    {
        ueventWait((UserEvent *)arg);
        GPIO_ToggleBits(GPIOD, GPIO_Pin_15); // blink blue LED
    }
}

/** Thread which periodically attempts to acquire a semaphore and turns a
  * red LED on when it does.
  * \param arg Number of cycles to leave red LED on and off for.
  */
static void semFlashRedThread(void *arg)
{
    // This loop uses the demo_sem semaphore. demo_sem is set up to have
    // a maximum count of 1. That makes it a mutex.
    // Mutex acquire = usemWait(), mutex release = usemSignal().
    while(true)
    {
        uschedSleep((uint32_t)arg);
        // The following usemWait() call will block until the semaphore can
        // be acquired.
        usemWait(&demo_sem);
        GPIO_SetBits(GPIOD, GPIO_Pin_14); // turn on red LED
        uschedSleep((uint32_t)arg);
        GPIO_ResetBits(GPIOD, GPIO_Pin_14); // turn off red LED
        // The following usemSignal() call releases the semaphore.
        usemSignal(&demo_sem);
        // It's usually important to sleep (or otherwise block)
        // after usemSignal(), to give other waiting threads some CPU time to
        // try and acquire the (now released) semaphore. Since this microkernel
        // uses a co-operative scheduler, failing to sleep may prevent other
        // threads from ever getting any CPU time.
    }
}

/** Initialise thread-safe FIFO structure.
  * \param fifo Structure to initialise. */
static void fifoInit(struct FIFO *fifo)
{
    fifo->available = 0;
    fifo->next = 0;
    usemInit(&(fifo->mutex), 1);
    ueventInit(&(fifo->not_empty), false);
    ueventInit(&(fifo->not_full), false);
    ueventReset(&(fifo->not_empty));
    ueventSet(&(fifo->not_full));
}

/** Get one item from a thread-safe FIFO. If the FIFO is empty, this will
  * block until a producer puts an item into the FIFO.
  * Multiple threads can call this function on the same FIFO, but the
  * are no fairness guarantees.
  * \param fifo Structure which has been previously initialised
  *             using fifoInit().
  * \return One item from the FIFO.
  */
static uint8_t fifoGet(struct FIFO *fifo)
{
    uint8_t r;

    // Attempt to acquire the FIFO protection mutex.
    usemWait(&(fifo->mutex));
    // This while loop is essential. Because the usemSignal() - ueventWait() -
    // usemWait() sequence is not atomic, another thread can "steal" a FIFO
    // item by responding to the fifo->not_empty even in-between the
    // ueventWait() and usemWait(). The while loop checks for this case.
    while (fifo->available == 0) // is FIFO empty?
    {
        // The FIFO protection mutex must be released to give producers
        // a chance to modify the FIFO.
        usemSignal(&(fifo->mutex));
        // Wait for the FIFO to become not empty.
        // Note that there is no chance for produced items to be "lost",
        // because the fifo->not_empty event does not get reset until a
        // consumer fully consumes the FIFO.
        ueventWait(&(fifo->not_empty));
        // Re-acquire FIFO protection mutex before checking if FIFO is
        // empty again.
        usemWait(&(fifo->mutex));
    }
    // We can now safely assume that at this point:
    // available != 0 and mutex acquired.

    // Get one item from FIFO.
    r = fifo->storage[fifo->next];
    fifo->next = (fifo->next + 1) % FIFO_SIZE;
    fifo->available--;

    // FIFO is now guaranteed to be not full. Setting the fifo->not_full event
    // may unblock some producers.
    ueventSet(&(fifo->not_full));
    // The fifo->not_empty event is only reset when the FIFO is empty. This
    // ensures that no consumers will block if there are items to consume.
    if (fifo->available == 0)
    {
        ueventReset(&(fifo->not_empty)); // FIFO is empty
    }
    // Release FIFO protection mutex, now that we're done.
    usemSignal(&(fifo->mutex));
    return r;
}

/** Put one item into a thread-safe FIFO. If the FIFO is full, this will
  * block until a consumer gets an item from the FIFO.
  * Multiple threads can call this function on the same FIFO, but the
  * are no fairness guarantees.
  * \param fifo Structure which has been previously initialised
  *             using fifoInit().
  * \param item The item to put into the FIFO.
  */
static void fifoPut(struct FIFO *fifo, uint8_t item)
{
    unsigned int temp;

    // The synchronisation in this function is almost identical to fifoGet(),
    // except that "full" and "empty" events and checks are swapped.
    usemWait(&(fifo->mutex));
    while (fifo->available == FIFO_SIZE) // is FIFO full?
    {
        usemSignal(&(fifo->mutex));
        ueventWait(&(fifo->not_full));
        usemWait(&(fifo->mutex));
    }

    // Put one item into FIFO.
    temp = fifo->next; // avoid "order of volatile accesses is undefined" warning
    fifo->storage[(temp + fifo->available) % FIFO_SIZE] = item;
    fifo->available++;

    ueventSet(&(fifo->not_empty));
    if (fifo->available == FIFO_SIZE)
    {
        ueventReset(&(fifo->not_full)); // FIFO is full
    }
    usemSignal(&(fifo->mutex));
}

/** Thread which writes sequential values to a thread-safe FIFO.
  * \param arg The FIFO to write to (should be a pointer to a #FIFO structure).
  */
static void fifoWriteThread(void *arg)
{
    uint8_t counter;

    counter = 0;
    while(true)
    {
        // Write sequential values to the FIFO, so that the read thread can
        // verify that the items come out in the right order.
        fifoPut((struct FIFO *)arg, counter);
        counter++;
        // Sleep for a random amount of time. This is supposed to imitate
        // real-world usage of FIFOs, where writes can occur with unpredictable
        // timing.
        uschedSleep(((rand() % 256) * 20000) + 16800);
    }
}

/** Thread which reads values from a thread-safe FIFO and verifies that they
  * are sequential.
  * \param arg The FIFO to read from (should be a pointer to a #FIFO structure).
  */
static void fifoReadThread(void *arg)
{
    uint8_t compare_counter;
    uint8_t item;

    compare_counter = 0;
    while(true)
    {
        // Sleep for a random amount of time. This is supposed to imitate
        // real-world usage of FIFOs, where reads can occur with unpredictable
        // timing.
        uschedSleep(((rand() % 256) * 20000) + 16800);
        item = fifoGet((struct FIFO *)arg);
        if (item == compare_counter)
        {
            // Items have sequential value. Toggle orange LED to tell user
            // that everything is okay.
            compare_counter++;
            GPIO_ToggleBits(GPIOD, GPIO_Pin_13); // blink orange LED
        }
        else
        {
            // Sequence mismatch; the FIFO isn't working as expected.
            // Turn off orange LED for an indefinite amount of time to
            // indicate an error condition.
            GPIO_ResetBits(GPIOD, GPIO_Pin_13); // turn off orange LED
            uschedExitThread();
        }
    }
}
