/** \file interrupts.c
  *
  * \brief Manages interrupts.
  *
  * Threads can handle interrupts by waiting on an event. This results in some
  * quite horrible interrupt latency, but it's an easy way to deal with
  * interrupts without giving processes access to privileged mode (which is
  * what would happen if ISRs executed in an ARM Cortex-M4F Handler context).
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"
#include "stm32f4xx.h"

/** Events which will be set when an interrupt is received. */
static KernelEvent irq_events[NUM_IRQ];
/** Array to keep track of which interrupts are enabled or disabled.
  * Why not just use the NVIC? Well, that isn't reliable because
  * Kernel_IRQHandler() disables interrupts whenever they're triggered. */
static bool irq_enabled[NUM_IRQ];

/** Prepare interrupt handlers. For obvious reasons, this should be called
  * before interrupts are enabled. */
void kinterruptInit(void)
{
    int i;

    for (i = 0; i < NUM_IRQ; i++)
    {
        keventInit(&(irq_events[i]), true);
    }
}

/** Wait for an interrupt to be triggered. Make sure the interrupt is
  * enabled (see kinterruptEnable()).
  * \param irq IRQ number of interrupt to wait for.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the specified IRQ was invalid, and #KERNEL_RETURN_IRQ_PERMISSION
  *         if the current process doesn't have access to the specified IRQ.
  */
KernelReturn kinterruptWait(uint8_t irq)
{
    if (irq >= NUM_IRQ)
    {
        return KERNEL_RETURN_INVALID_PARAM;
    }
    enterCriticalSection();
    if (kprocessIsCurrentProcessIRQOwner(irq))
    {
        keventWait(&(irq_events[irq]));
        leaveCriticalSection();
        return KERNEL_RETURN_SUCCESS;
    }
    else
    {
        leaveCriticalSection();
        return KERNEL_RETURN_IRQ_PERMISSION;
    }
}

/** Enable interrupt, so that it can be triggered. Interrupts are all disabled
  * by default.
  * \param irq IRQ number of interrupt to enable.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the specified IRQ was invalid, and #KERNEL_RETURN_IRQ_PERMISSION
  *         if the current process doesn't have access to the specified IRQ.
  */
KernelReturn kinterruptEnable(uint8_t irq)
{
    if (irq >= NUM_IRQ)
    {
        return KERNEL_RETURN_INVALID_PARAM;
    }
    enterCriticalSection();
    if (kprocessIsCurrentProcessIRQOwner(irq))
    {
        // Set priority to be just below SysTick. The relative NVIC priority of
        // external interrupts doesn't matter because the actual handling
        // is done by threads (which have their own priority system).
        NVIC_SetPriority((IRQn_Type)irq, 0x01);
        NVIC_EnableIRQ((IRQn_Type)irq);
        irq_enabled[irq] = true;
        leaveCriticalSection();
        return KERNEL_RETURN_SUCCESS;
    }
    else
    {
        leaveCriticalSection();
        return KERNEL_RETURN_IRQ_PERMISSION;
    }
}

/** Disable interrupt, so that it will not be triggered.
  * \param irq IRQ number of interrupt to disable.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_INVALID_PARAM if
  *         the specified IRQ was invalid, and #KERNEL_RETURN_IRQ_PERMISSION
  *         if the current process doesn't have access to the specified IRQ.
  */
KernelReturn kinterruptDisable(uint8_t irq)
{
    if (irq >= NUM_IRQ)
    {
        return KERNEL_RETURN_INVALID_PARAM;
    }
    enterCriticalSection();
    if (kprocessIsCurrentProcessIRQOwner(irq))
    {
        NVIC_DisableIRQ((IRQn_Type)irq);
        irq_enabled[irq] = false;
        leaveCriticalSection();
        return KERNEL_RETURN_SUCCESS;
    }
    else
    {
        leaveCriticalSection();
        return KERNEL_RETURN_IRQ_PERMISSION;
    }
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
KernelReturn kinterruptAcknowledge(uint8_t irq, bool is_level_sensitive)
{
    if (irq >= NUM_IRQ)
    {
        return KERNEL_RETURN_INVALID_PARAM;
    }
    enterCriticalSection();
    if (kprocessIsCurrentProcessIRQOwner(irq))
    {
        if (is_level_sensitive)
        {
            // Level-sensitive means that the interrupt will have been
            // re-triggered upon return from Kernel_IRQHandler() (since
            // Kernel_IRQHandler() doesn't actually deal with the interrupt).
            // Clear the pending IRQ in order to avoid a spurious trigger.
            NVIC_ClearPendingIRQ((IRQn_Type)irq);
        }
        if (irq_enabled[irq])
        {
            // Kernel_IRQHandler() disabled the interrupt, so re-enable it
            // so that it can trigger again.
            NVIC_EnableIRQ((IRQn_Type)irq);
        }
        leaveCriticalSection();
        return KERNEL_RETURN_SUCCESS;
    }
    else
    {
        leaveCriticalSection();
        return KERNEL_RETURN_IRQ_PERMISSION;
    }
}

/** This is the actual handler for non-system interrupts. This mainly just
  * sets one of the events in #irq_events, so that a thread can do the
  * actual handling (in an unprivileged Thread context, instead of a
  * privileged Handler context). */
void Kernel_IRQHandler(void)
{
    uint32_t irq;

    irq = __get_IPSR();
    if (irq >= 16)
    {
        // The IPSR register contains the exception number. Exception numbers
        // are offset by 16 compared to IRQ numbers, in order to include the
        // system exceptions.
        irq = (irq - 16) & 0xff;
        if (irq < NUM_IRQ)
        {
            keventSet(&(irq_events[irq]));
            // The NVIC will normally suppress an interrupt from triggering
            // again until ISR return. This assumes that the ISR will actually
            // deal with the interrupt. That's not true in our case - the
            // actual handling is done by another thread. Thus the interrupt
            // has to be explicitly disabled so that it doesn't continuously
            // fire after return from this ISR.
            NVIC_DisableIRQ((IRQn_Type)irq);
        }
    }
}
