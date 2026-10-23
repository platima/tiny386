/*
 * Loads and stores of 16 and 32 bits in guest memory, at any address.
 *
 * The emulator reads and writes guest memory through pointers that are
 * as aligned as the guest's address was, and x86 code does not align
 * much: the 32-bit immediate of an instruction is at an odd address more
 * often than not.  That is fine where the host does such an access in
 * hardware.  Where it does not, each one is a trap into the kernel or the
 * firmware, which carries it out by hand: it works, some hundreds of
 * times slower, and nothing says so.
 *
 * With I386_ALIGNED_ONLY defined, an access that is not aligned is made
 * of smaller ones that are.  What is loaded and stored is the same.  A
 * compiler told that the host does misaligned accesses quickly joins them
 * into one again: GCC does so for the ESP32-S31 (-mtune=esp32s31), where
 * each of these is one load or store at any address, the stores behind a
 * test whose two arms are alike.  The S31's cores carry out misaligned
 * loads and stores themselves (ESP-IDF has no handler for the trap, and
 * the board runs Windows 95).
 */
#ifndef UNALIGNED_H
#define UNALIGNED_H

#include <stdint.h>

#ifdef I386_ALIGNED_ONLY

static inline uint16_t ldu16(const uint8_t *p)
{
	if (__builtin_expect(!((uintptr_t) p & 1), 1))
		return *(const uint16_t *) p;
	return p[0] | (p[1] << 8);
}

static inline uint32_t ldu32(const uint8_t *p)
{
	if (__builtin_expect(!((uintptr_t) p & 3), 1))
		return *(const uint32_t *) p;
	if ((uintptr_t) p & 1)
		return p[0] | ((uint32_t) *(const uint16_t *) (p + 1) << 8) |
			((uint32_t) p[3] << 24);
	return *(const uint16_t *) p |
		((uint32_t) *(const uint16_t *) (p + 2) << 16);
}

static inline void stu16(uint8_t *p, uint16_t val)
{
	if (__builtin_expect(!((uintptr_t) p & 1), 1)) {
		*(uint16_t *) p = val;
	} else {
		p[0] = val;
		p[1] = val >> 8;
	}
}

static inline void stu32(uint8_t *p, uint32_t val)
{
	if (__builtin_expect(!((uintptr_t) p & 3), 1)) {
		*(uint32_t *) p = val;
	} else if ((uintptr_t) p & 1) {
		p[0] = val;
		*(uint16_t *) (p + 1) = val >> 8;
		p[3] = val >> 24;
	} else {
		*(uint16_t *) p = val;
		*(uint16_t *) (p + 2) = val >> 16;
	}
}

#else

static inline uint16_t ldu16(const uint8_t *p)
{
	return *(const uint16_t *) p;
}

static inline uint32_t ldu32(const uint8_t *p)
{
	return *(const uint32_t *) p;
}

static inline void stu16(uint8_t *p, uint16_t val)
{
	*(uint16_t *) p = val;
}

static inline void stu32(uint8_t *p, uint32_t val)
{
	*(uint32_t *) p = val;
}

#endif

#endif /* UNALIGNED_H */
