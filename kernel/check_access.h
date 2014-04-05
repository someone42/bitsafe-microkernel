/** \file check_access.h
  *
  * \brief Describes MPU access checking functions.
  *
  * Many kernel functions (eg. queue functions) access memory through pointers
  * passed from userspace functions. Since kernel functions execute in a
  * privileged context, these memory accesses need to be explicitly checked in
  * a userspace (unprivileged) context by the MPU.
  *
  * This file is licensed as described by the file LICENCE.
  */

#ifndef CHECK_ACCESS_H_INCLUDED
#define CHECK_ACCESS_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

// Doxygen annotations are here, because they can't be in asm file.

/** Check that the current process has read/write access to a byte of memory.
  * Permissions violations will cause a memory management fault.
  * \param addr Address of byte.
  */
extern void checkAccessByte(uint8_t *addr);
/** Check that the current process has read/write access to a word (32 bits)
  * of memory. Permissions violations will cause a memory management fault.
  * \param addr Address of word.
  */
extern void checkAccessWord(uint32_t *addr);
/** Check that the current process has permission to read a byte of memory,
  * then read that byte. Permissions violations will cause a memory management
  * fault.
  * \param addr Address of byte.
  * \return Contents of byte.
  */
extern uint8_t loadCheckedByte(uint8_t *addr);
/** Check that the current process has permission to read a word (32 bits) of
  * memory, then read that word. Permissions violations will cause a memory
  * management fault.
  * \param addr Address of word.
  * \return Contents of word.
  */
extern uint32_t loadCheckedWord(uint32_t *addr);
/** Check that the current process has permission to write to a byte of memory,
  * then write to that byte. Permissions violations will cause a memory
  * management fault.
  * \param addr Address of byte.
  * \param value Value to write.
  */
extern void storeCheckedByte(uint8_t *addr, uint8_t value);
/** Check that the current process has permission to write to a word (32 bits)
  * of memory, then write to that word. Permissions violations will cause a
  * memory management fault.
  * \param addr Address of word.
  * \param value Value to write.
  */
extern void storeCheckedWord(uint32_t *addr, uint32_t value);

#endif // #ifndef CHECK_ACCESS_H_INCLUDED
