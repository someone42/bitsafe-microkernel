/** \file crit_sect.c
  *
  * \brief Kernel critical sections.
  *
  * Kernel critical sections are used to ensure that sequences of kernel
  * operations are not interrupted.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"
#include "stm32f4xx.h"

/** Critical section nesting counter. 0 means no-one has critical section,
  * non-zero means critical section is taken. */
static volatile unsigned int critical_section_counter;

/** Enter kernel critical section. Use this to make a sequence of kernel
  * operations atomic. Critical sections can be nested. */
void enterCriticalSection(void)
{
    __disable_irq(); // a bit heavy handed, but it works
    __DSB();
    __ISB();
    critical_section_counter++;
}

/** Leave kernel critical section. Every call to enterCriticalSection() must
  * eventually be matched with a call to this function. */
void leaveCriticalSection(void)
{
    critical_section_counter--;
    if (critical_section_counter == 0)
    {
        __enable_irq();
    }
}
