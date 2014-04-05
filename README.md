bitsafe-microkernel
===================

Microkernel for BitSafe.

## Introduction

For security purposes, the functional modules in the BitSafe are divided into
processes with mutually-exclusive memory regions. Each process operates in a
sandbox of its own exclusive resources (memory, interrupts and peripherals).
That way, exploits which break one process should be mostly contained within
that process.

The microkernel provides the following services:
- Events, for synchronisation between threads
- Semaphores, for synchronisation between threads
- Scheduling, for multi-threading
- Inter-process calls, for communication between processes
- Interrupt management, for safe handling of interrupts in usermode

## Documentation

Userspace should
See userspace/userspace_lib.c for a list of system calls and their associated
documentation.

If you prefer examples, see examples.c for some annotated examples which use
various microkernel functions.

## "No processes" mode

To aid with development, the project comes distributed with the NO_PROCESSES
preprocessor symbol defined. This disables all process-related functions:
inter-process calls, queues and interrupt management. Everything takes place
in one monolithic process which has access to everything.

This mode provides no security, but it makes development easier. Partitioning
code into processes is probably going to be easier if there is a concrete
implementation of each process.

## Implementation information

The microkernel is written for the STM32F4xx series of microcontrollers.
A project file is provided for IAR Embedded Workbench for ARM.
examples.c is intended to run on the STM32F4 Discovery Kit.
The multi-threading model is co-operative, with priorities. Hence threads will
run until they block/sleep, or a thread of higher priority unblocks/finishes
sleeping.