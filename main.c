/** \file main.c
  *
  * \brief Entry point of kernel.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> // for definition of NULL
#include "stm32f4xx.h"

#include "kernel/kernel.h"
#include "userspace/userspace_lib.h" // for definition of ARRAY_AS_STACK

/** Space for idle thread's stack. Enough space for about 4 nested
  * exceptions. */
static uint8_t idle_thread_stack[256];
/** Space for startup thread's stack. Enough space for about 8 nested
  * exceptions. */
static uint8_t startup_thread_stack[512];

/** Hard fault handler that retrieves some additional diagnostics
  * information for a debugger to look at. */
void HardFault_Handler(void)
{
    volatile uint32_t hfsr, cfsr;
    volatile uint32_t stacked_pc;
    volatile unsigned int junk;
    uint32_t *psp;

    // Get information about what caused the HardFault. See the STM Cortex-M4
    // programming manual (PM0214, "STM32F3xxx and STM32F4xxx Cortex-M4
    // programming manual") for more information on the interpretation of
    // these registers.
    hfsr = SCB->HFSR; // HardFault Status Register
    cfsr = SCB->CFSR; // Configurable Fault Status Register
    psp = (uint32_t *)__get_PSP(); // Process Stack Pointer
    stacked_pc = psp[6];

    // Put breakpoint here:
    while(true)
    {
        junk++;
    }
}

/** This will be called when something really bad happens in the kernel,
  * for example if an assert fails. */
void kernelFatalError(void)
{
    __disable_irq();
    HardFault_Handler();
}

/** Thread which will run when no other thread is running. This is required,
  * because the scheduler expects there to always be at least one schedulable
  * thread. */
void idleThread(void *arg)
{
    while (true)
    {
        __WFI();
    }
}

/** Userspace entry point.
  *
  * This is the first thread that will be scheduled. Startup tasks should be
  * done in this thread. Since the startup thread has the highest priority but
  * limited stack space, you should only perform startup functions and then
  * call uschedExitThread() as soon as possible. */
extern void startupThread(void *arg);

/** Entry point. */
int main()
{
    ksyscallInit();
    kinterruptInit();

    __enable_irq();

    kprocessInit();
    kprocessSwitchProcess(0); // need this or many things fail

    kschedCreateThread(idleThread, ARRAY_AS_STACK(idle_thread_stack), NULL, PRIORITY_IDLE);
    kschedCreateThread(startupThread, ARRAY_AS_STACK(startup_thread_stack), NULL, PRIORITY_HIGHEST);
    kschedStart();

    while (true)
    {
        __WFI();
    }
}
