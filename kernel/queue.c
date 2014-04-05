/** \file queue.c
  *
  * \brief Kernel queues.
  *
  * Queues are a thread-safe way for processes to communicate with each other.
  * Queues can be written to and then read from in a FIFO manner. Reading
  * from an empty queue will block a thread. Writing to a full queue will
  * also block a thread.
  *
  * Because kernel queues are shared, and there's a limited
  * number of them, they should only be used for inter-process communication.
  * Each kernel queue is duplex, but is actually two underlying simplex
  * queues: one for the caller of an inter-process call (IPC) to write to, and
  * one for the callee of an IPC to write to.
  *
  * This file is licensed as described by the file LICENCE.
  */

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"
#include "check_access.h"

#include "process_table_num.h"

/** Maximum number of open queues. This defines the size of #queue_table, so
  * it has a direct effect on kernel RAM usage. */
#define MAX_NUM_QUEUES          12
/** Maximum number of open queues per process. This should be smaller
  * than #MAX_NUM_QUEUES, so that any one process cannot monopolize
  * (accidentally or otherwise) the kernel queues. */
#ifdef NO_PROCESSES
#define QUEUES_PER_PROCESS      MAX_NUM_QUEUES
#else // #ifdef NO_PROCESSES
#define QUEUES_PER_PROCESS      4
#endif // #ifdef NO_PROCESSES
/** Size of each FIFO in each queue (note that each queue has two FIFOs).
  * This has a direct effect on kernel RAM usage. Making this larger will
  * make bulk data transfer between processes more efficient (less context
  * switches per byte). */
#define FIFO_SIZE               128

/** AND mask applied to queue handle to get an index into #queue_table.
  * This is used so that the queue handle can contain more information than
  * just the index. */
#define INDEX_MASK              0xff
/** Amount to shift sequence when generating a queue handle. This should be
  * enough to get the sequence "out of the way" of the index bits. */
#define SEQUENCE_SHIFT          8

/** A circular buffer (a.k.a. FIFO). */
typedef struct CircularBufferStruct
{
    /** Index of the next element to remove. */
    uint32_t next;
    /** Number of elements remaining in buffer. */
    uint32_t remaining;
    /** Storage for the buffer. */
    uint8_t storage[FIFO_SIZE];
    /** Set when not empty, reset when empty. */
    KernelEvent not_empty_event;
    /** Set when not full, reset when empty. */
    KernelEvent not_full_event;
} CircularBuffer;

/** Kernel queue table entry (see #queue_table). Note that each kernel queue is
  * actually two "queues": one for each direction. */
typedef struct QueueStruct
{
    /** Whether this entry has been claimed by a process. */
    uint8_t opened;
    /** The process ID of the process which claimed this entry
      * (source, or caller). */
    uint8_t source_process_id;
    /** The process ID of the "other side" of the queue.
      * (destination, or callee). */
    uint8_t dest_process_id;
    uint8_t padding; // align next field to 4-byte boundary
    /** Handle sequence (incremented every time the entry is claimed), used to
      * try to catch bugs where a process accidentally uses a closed handle. */
    uint32_t sequence;
    /** Most recently generated handle for this entry. */
    QueueHandle generated_handle;
    /** FIFO for forwards (source to destination) traffic. */
    CircularBuffer forward_fifo;
    /** FIFO for backwards (destination to source) traffic. */
    CircularBuffer backward_fifo;
} Queue;

/** Static table of kernel queues. Processes can claim individual entries of
  * this table. There's probably better ways to implement shared queues, but
  * a static table is so simple! */
Queue queue_table[MAX_NUM_QUEUES];

/** Number of open queues for each process. */
uint8_t process_queue_counter[NUM_PROCESSES];

/** Clear and initialise contents of circular buffer.
  * \param buffer The circular buffer to initialise and clear.
  * \warning This should only be called from within a critical section - for
  *          efficiency reasons the function doesn't itself
  *          call enterCriticalSection().
  */
static void initCircularBuffer(CircularBuffer *buffer)
{
    buffer->next = 0;
    buffer->remaining = 0;
    keventInit(&(buffer->not_empty_event), false);
    keventInit(&(buffer->not_full_event), false);
    keventReset(&(buffer->not_empty_event));
    keventSet(&(buffer->not_full_event));
}

/** Check whether a circular buffer is empty.
  * \param buffer The circular buffer to check.
  * \return true if it is empty, false if it is non-empty.
  * \warning This should only be called from within a critical section - for
  *          efficiency reasons the function doesn't itself
  *          call enterCriticalSection().
  */
static bool isCircularBufferEmpty(CircularBuffer *buffer)
{
    return buffer->remaining == 0;
}

/** Check whether a circular buffer is full.
  * \param buffer The circular buffer to check.
  * \return true if it is full, false if it is not full.
  * \warning This should only be called from within a critical section - for
  *          efficiency reasons the function doesn't itself
  *          call enterCriticalSection().
  */
static bool isCircularBufferFull(CircularBuffer *buffer)
{
    return buffer->remaining == FIFO_SIZE;
}

/** Read bytes from a circular buffer. This is non-blocking - for example,
  * if the circular buffer is empty, this will immediately return with a
  * return value of 0.
  * \param buffer The circular buffer to read from.
  * \param data Buffer to place bytes read into.
  * \param length Maximum number of bytes to read (i.e. size of data, in
  *               bytes).
  * \return The actual number of bytes read from the circular buffer.
  * \warning This should only be called from within a critical section - for
  *          efficiency reasons the function doesn't itself
  *          call enterCriticalSection().
  */
static uint32_t circularBufferRead(CircularBuffer *buffer, uint8_t *data, uint32_t length)
{
    uint32_t bytes_read;

    bytes_read = 0;
    while (!isCircularBufferEmpty(buffer) && (bytes_read < length))
    {
        storeCheckedByte(&(data[bytes_read]), buffer->storage[buffer->next]);
        buffer->remaining--;
        buffer->next = (buffer->next + 1) & (FIFO_SIZE - 1);
        bytes_read++;
    }
    if (isCircularBufferEmpty(buffer))
    {
        keventReset(&(buffer->not_empty_event));
    }
    if (bytes_read > 0)
    {
        // Buffer is guaranteed to not be full.
        keventSet(&(buffer->not_full_event));
    }
    return bytes_read;
}

/** Write bytes to a circular buffer. This is non-blocking - for example,
  * if the circular buffer is full, this will immediately return with a
  * return value of 0.
  * \param buffer The circular buffer to write to.
  * \param data The bytes to write to the buffer.
  * \param length Number of bytes to write (i.e. size of the data, in bytes).
  * \return The actual number of bytes that were written to the buffer.
  * \warning This should only be called from within a critical section - for
  *          efficiency reasons the function doesn't itself
  *          call enterCriticalSection().
  */
static uint32_t circularBufferWrite(CircularBuffer *buffer, uint8_t *data, uint32_t length)
{
    uint32_t index;
    uint32_t bytes_written;

    bytes_written = 0;
    while (!isCircularBufferFull(buffer) && (bytes_written < length))
    {
        index = (buffer->next + buffer->remaining) & (FIFO_SIZE - 1);
        buffer->storage[index] = loadCheckedByte(&(data[bytes_written]));
        buffer->remaining++;
        bytes_written++;
    }
    if (isCircularBufferFull(buffer))
    {
        keventReset(&(buffer->not_full_event));
    }
    if (bytes_written > 0)
    {
        // Buffer is guaranteed to not be empty.
        keventSet(&(buffer->not_empty_event));
    }
    return bytes_written;
}

/** Generate a new handle which can be used to refer a kernel queue.
  * \param index Index into #queue_table of entry to generate handle for.
  * \return Generated handle.
  */
static QueueHandle generateHandle(unsigned int index)
{
    // Why bother including a sequence in the handle? The source can
    // asynchronously close a queue, and then open a new queue which happens
    // to occupy the same queue table entry. If the queue handle only
    // included the queue table index, then the destination could accidentally
    // begin using the new queue, with unexpected consequences.
    return (queue_table[index].sequence << SEQUENCE_SHIFT) | (index & INDEX_MASK);
}

/** Check whether a queue handle is valid and refers to an opened queue.
  * This does not check whether the current process has permission to access
  * the queue, because permissions are more fine-grained than "can I access
  * this handle?".
  * \param handle Queue handle to check.
  * \param out_index If the queue handle is valid, the index
  *                  (into #queue_table) of the opened queue will be written
  *                  here.
  * \return true if handle is valid, false if not.
  */
static bool isHandleValid(QueueHandle handle, unsigned int *out_index)
{
    unsigned int index;

    index = handle & INDEX_MASK;
    if (index >= MAX_NUM_QUEUES)
    {
        return false;
    }
    if (!queue_table[index].opened || (queue_table[index].generated_handle != handle))
    {
        return false;
    }
    *out_index = index;
    return true;
}

/** Claim an entry in the kernel queue table (#queue_table). This must be
  * called before reading or writing anything to the queue. Because
  * the number of kernel queue table entries is static and limited, you
  * should call kqueueClose() as soon as the queue is no longer needed.
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
KernelReturn kqueueOpen(uint8_t dest_process_id, QueueHandle *out_handle)
{
    unsigned int i;
    QueueHandle handle;
    uint8_t current_process_id;

    if (dest_process_id >= NUM_PROCESSES)
    {
        return KERNEL_RETURN_INVALID_PARAM;
    }

    enterCriticalSection();

    current_process_id = kprocessGetCurrent();
    KERNEL_ASSERT(current_process_id < NUM_PROCESSES);
    if (process_queue_counter[current_process_id] >= QUEUES_PER_PROCESS)
    {
        leaveCriticalSection();
        return KERNEL_RETURN_PROCESS_NO_FREE;
    }

    // Find a free entry.
    for (i = 0; i < MAX_NUM_QUEUES; i++)
    {
        if (!queue_table[i].opened)
        {
            // Claim entry.
            queue_table[i].opened = true;
            queue_table[i].source_process_id = current_process_id;
            queue_table[i].dest_process_id = dest_process_id;
            queue_table[i].sequence++;
            handle = generateHandle(i);
            queue_table[i].generated_handle = handle;
            initCircularBuffer(&(queue_table[i].forward_fifo));
            initCircularBuffer(&(queue_table[i].backward_fifo));
            *out_handle = handle;
            process_queue_counter[current_process_id]++;
            leaveCriticalSection();
            return KERNEL_RETURN_SUCCESS;
        }
    }

    // Could not find a free entry.
    leaveCriticalSection();
    return KERNEL_RETURN_GLOBAL_NO_FREE;
}

/** Close a queue. This frees up one entry in the kernel queue
  * table (#queue_table).
  * \param handle Queue handle of queue to close.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_BAD_HANDLE if
  *         the supplied queue handle was invalid.
  */
KernelReturn kqueueClose(QueueHandle handle)
{
    unsigned int index;
    uint8_t current_process_id;

    enterCriticalSection();
    if (!isHandleValid(handle, &index))
    {
        leaveCriticalSection();
        return KERNEL_RETURN_BAD_HANDLE;
    }
    current_process_id = kprocessGetCurrent();

    // Only the source may close a queue. This is for two reasons:
    // - The source will probably still need to read return values from an
    //   inter-process call after the destination has finished servicing the
    //   call.
    // - The queue counts against the source's "number of queues" limit, so
    //   the source should be able to deterministically close a queue to
    //   conveniently manage this limit.
    if (current_process_id != queue_table[index].source_process_id)
    {
        leaveCriticalSection();
        return KERNEL_RETURN_BAD_HANDLE;
    }

    queue_table[index].opened = false;
    // Set all events to release all waiting threads. This ensures that
    // none of the events are active, so it is safe to call keventInit
    // on them again.
    keventSet(&(queue_table[index].forward_fifo.not_empty_event));
    keventSet(&(queue_table[index].forward_fifo.not_full_event));
    keventSet(&(queue_table[index].backward_fifo.not_empty_event));
    keventSet(&(queue_table[index].backward_fifo.not_full_event));
    // Don't memset the queue entry to all 00s, because that would reset
    // the sequence field.

    KERNEL_ASSERT(current_process_id < NUM_PROCESSES);
    process_queue_counter[current_process_id]--;

    leaveCriticalSection();
    return KERNEL_RETURN_SUCCESS;
}

/** Write data to a queue. This data can be read from the other end using
  * kqueueRead(). This does enforce traffic directions: only the source may
  * write in the forwards direction, and only the destination may write in the
  * backwards direction.
  * \param handle Queue handle of queue to write to.
  * \param is_forward Specifies direction of write: true = forwards,
  *                   false = backwards.
  * \param data Data to write to the queue.
  * \param length Maximum number of bytes to write to the queue.
  * \param out_written Upon success, the actual number of bytes written to the
  *                    queue will be written here.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_BAD_HANDLE if
  *         the supplied queue handle was invalid.
  */
KernelReturn kqueueWrite(QueueHandle handle, bool is_forward, uint8_t *data, uint32_t length, uint32_t *out_written)
{
    CircularBuffer *buffer;
    uint8_t allowed_process_id;
    unsigned int index;
    uint32_t written;

    enterCriticalSection();
    if (!isHandleValid(handle, &index))
    {
        leaveCriticalSection();
        return KERNEL_RETURN_BAD_HANDLE;
    }

    if (is_forward)
    {
        buffer = &(queue_table[index].forward_fifo);
        allowed_process_id = queue_table[index].source_process_id;
    }
    else
    {
        buffer = &(queue_table[index].backward_fifo);
        allowed_process_id = queue_table[index].dest_process_id;
    }
    if (kprocessGetCurrent() != allowed_process_id)
    {
        leaveCriticalSection();
        return KERNEL_RETURN_BAD_HANDLE;
    }

    written = circularBufferWrite(buffer, data, length);
    storeCheckedWord(out_written, written);
    if (written != length)
    {
        // Thread needs to wait until there's more space in the buffer.
        keventWait(&(buffer->not_full_event));
    }
    leaveCriticalSection();
    return KERNEL_RETURN_SUCCESS;
}

/** Read data from a queue. This reads the data that was written to the other
  * end using kqueueWrite(). This does enforce traffic directions: only the
  * destination may read in the forwards direction, and only the source may
  * read in the backwards direction.
  * \param handle Queue handle of queue to read from
  * \param is_forward Specifies direction of read: true = forwards,
  *                   false = backwards.
  * \param data Buffer to store the bytes that were read from the queue.
  * \param size Maximum number of bytes to read from the queue.
  * \param out_read Upon success, the actual number of bytes read from the
  *                 queue will be written here.
  * \return #KERNEL_RETURN_SUCCESS on success, #KERNEL_RETURN_BAD_HANDLE if
  *         the supplied queue handle was invalid.
  */
KernelReturn kqueueRead(QueueHandle handle, bool is_forward, uint8_t *data, uint32_t size, uint32_t *out_read)
{
    CircularBuffer *buffer;
    uint8_t allowed_process_id;
    unsigned int index;
    uint32_t read;

    enterCriticalSection();
    if (!isHandleValid(handle, &index))
    {
        leaveCriticalSection();
        return KERNEL_RETURN_BAD_HANDLE;
    }

    if (is_forward)
    {
        buffer = &(queue_table[index].forward_fifo);
        allowed_process_id = queue_table[index].dest_process_id;
    }
    else
    {
        buffer = &(queue_table[index].backward_fifo);
        allowed_process_id = queue_table[index].source_process_id;
    }
    if (kprocessGetCurrent() != allowed_process_id)
    {
        leaveCriticalSection();
        return KERNEL_RETURN_BAD_HANDLE;
    }

    read = circularBufferRead(buffer, data, size);
    storeCheckedWord(out_read, read);
    if (read == 0)
    {
        // Thread needs to wait until someone else writes something to the
        // buffer.
        keventWait(&(buffer->not_empty_event));
    }
    leaveCriticalSection();
    return KERNEL_RETURN_SUCCESS;
}
