/** \file event.c
  *
  * \brief Kernel events.
  *
  * Events allow the kernel to synchronise threads. Events are the only
  * thing a thread will wait on - other waitable synchronisation primitives
  * must be built on top of events.
  *
  * Events can be auto-reset (useful for task dispatchers) or manual-reset
  * (useful for notifications). Multiple threads can wait on a single event.
  * For safety, it is not possible to delete events.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"

/** Initialise an event. Do not call this on an active event, otherwise
  * the event will end up in a weird state.
  * \param event Event structure to initialise.
  * \param auto_reset false = manual-reset event, true = auto-reset event.
  *                   An auto-reset event will reset itself after one thread
  *                   finishes waiting on it.
  */
void keventInit(KernelEvent *event, bool auto_reset)
{
    event->is_set = false;
    event->thread_waiting = false;
    event->is_auto_reset = auto_reset;
}

/** Causes the current thread to wait (block) on an event. The thread will
  * resume execution when the event is set (see keventSet()).
  * Multiple threads can wait on the same event.
  * \param event Kernel event to wait on.
  */
void keventWait(KernelEvent *event)
{
    enterCriticalSection();
    if (event->is_set)
    {
        // Event already set; no need to wait.
        if (event->is_auto_reset)
        {
            event->is_set = false;
        }
    }
    else
    {
        // Need to wait.
        event->thread_waiting = true;
        kschedWait(event);
    }
    leaveCriticalSection();
}

/** Set an event, so that any waiting threads will be awakened (unblocked).
  * If there are multiple waiting threads, then what happens depends on
  * whether the event is an auto-reset event (see keventInit()) or not.
  * - Setting an auto-reset event will only awaken one thread. This is because
  *   the event auto-resets after the first thread awakens.
  * - Setting a non-auto-reset event will awaken all waiting threads. This
  *   is because the event stays set after each thread awakens.
  * \param event Kernel event to set.
  */
void keventSet(KernelEvent *event)
{
    uint32_t num_waiting_threads;

    enterCriticalSection();
    event->is_set = true;
    if (event->thread_waiting)
    {
        if (event->is_auto_reset)
        {
            // Event auto-resets, so wake up one thread and then reset the
            // event.
            num_waiting_threads = kschedNotify(event, false);
            if (num_waiting_threads <= 1)
            {
                // No more waiting threads.
                event->thread_waiting = false;
            }
            event->is_set = false;
        }
        else
        {
            // Event does not auto-reset, so event stays "stuck" on and hence
            // should awaken every waiting thread.
            kschedNotify(event, true);
            event->thread_waiting = false;
        }
    }
    leaveCriticalSection();
}

/** Reset an event, so that threads will have to wait (block) on it.
  * You do not need to call this function for
  * auto-reset events (see keventInit()).
  * \param event Kernel event to reset.
  */
void keventReset(KernelEvent *event)
{
    enterCriticalSection();
    event->is_set = false;
    leaveCriticalSection();
}
