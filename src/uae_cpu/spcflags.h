 /*
  * UAE - The Un*x Amiga Emulator
  *
  * MC68000 emulation
  *
  * Copyright 1995 Bernd Schmidt
  */

#ifndef SPCFLAGS_H
#define SPCFLAGS_H

#include "SDL_compat.h"

enum {
	SPCFLAG_STOP			= 0x01,
	SPCFLAG_INTERNAL_IRQ	= 0x02,
	SPCFLAG_BRK				= 0x04,
	SPCFLAG_TRACE			= 0x08,
	SPCFLAG_DOTRACE			= 0x10,
	SPCFLAG_DOINT			= 0x20,
#ifdef USE_JIT
	SPCFLAG_JIT_END_COMPILE	= 0x40,
	SPCFLAG_JIT_EXEC_RETURN	= 0x80,
#else
	SPCFLAG_JIT_END_COMPILE	= 0,
	SPCFLAG_JIT_EXEC_RETURN	= 0,
#endif
	SPCFLAG_VBL			= 0x100,
	SPCFLAG_MFP			= 0x200,
	SPCFLAG_INT3		= 0x800,
	SPCFLAG_INT5		= 0x1000,
	SPCFLAG_SCC		= 0x2000,
	SPCFLAG_DSP		= 0x4000,
	SPCFLAG_ALL			= SPCFLAG_STOP
					| SPCFLAG_INTERNAL_IRQ
					| SPCFLAG_BRK
					| SPCFLAG_TRACE
					| SPCFLAG_DOTRACE
					| SPCFLAG_DOINT
					| SPCFLAG_JIT_END_COMPILE
					| SPCFLAG_JIT_EXEC_RETURN
					| SPCFLAG_INT3
					| SPCFLAG_VBL
					| SPCFLAG_INT5
					| SPCFLAG_SCC
					| SPCFLAG_MFP
					| SPCFLAG_DSP
					,

	SPCFLAG_ALL_BUT_EXEC_RETURN	= SPCFLAG_ALL & ~SPCFLAG_JIT_EXEC_RETURN

};

#define SPCFLAGS_TEST(m) \
	(__atomic_load_n(&regs.spcflags, __ATOMIC_ACQUIRE) & (m))

/* Macro only used in m68k_reset() */
#define SPCFLAGS_INIT(m) do { \
	__atomic_store_n(&regs.spcflags, (m), __ATOMIC_RELEASE); \
} while (0)

#ifndef ENABLE_EXCLUSIVE_SPCFLAGS

#define SPCFLAGS_SET(m) do { \
	__atomic_fetch_or(&regs.spcflags, (m), __ATOMIC_ACQ_REL); \
} while (0)

#define SPCFLAGS_CLEAR(m) do { \
	__atomic_fetch_and(&regs.spcflags, ~(m), __ATOMIC_ACQ_REL); \
} while (0)

#define SleepAndWait()	usleep(1000)

#elif (defined(CPU_i386) || defined(CPU_x86_64)) && defined(X86_ASSEMBLY) && !defined(ENABLE_REALSTOP)

#define HAVE_HARDWARE_LOCKS 1

#define SPCFLAGS_SET(m) do { \
	__asm__ __volatile__("lock\n\torl %1,%0" : "=m" (regs.spcflags) : "i" ((m))); \
} while (0)

#define SPCFLAGS_CLEAR(m) do { \
	__asm__ __volatile__("lock\n\tandl %1,%0" : "=m" (regs.spcflags) : "i" (~(m))); \
} while (0)

#define SleepAndWait()	usleep(1000)

#elif !defined(ENABLE_REALSTOP)

#undef HAVE_HARDWARE_LOCKS
extern  SDL_mutex *spcflags_lock;

#define SPCFLAGS_SET(m) do { 				\
	SDL_LockMutex(spcflags_lock);		\
	__atomic_fetch_or(&regs.spcflags, (m), __ATOMIC_ACQ_REL);					\
	SDL_UnlockMutex(spcflags_lock);	\
} while (0)

#define SPCFLAGS_CLEAR(m) do {				\
	SDL_LockMutex(spcflags_lock);		\
	__atomic_fetch_and(&regs.spcflags, ~(m), __ATOMIC_ACQ_REL);					\
	SDL_UnlockMutex(spcflags_lock);	\
} while (0)

#define SleepAndWait()	usleep(1000)

#else
/// Full STOP instruction implementation (default configuration)

#undef HAVE_HARDWARE_LOCKS
extern  SDL_mutex *spcflags_lock;
extern  SDL_cond *stop_condition;
#ifdef __cplusplus
extern "C"
#else
#include <stdbool.h>
#endif
bool cpu_stop_has_pending_work(void);

#define SPCFLAGS_SET(m) do { 				\
	SDL_LockMutex(spcflags_lock);		\
	__atomic_fetch_or(&regs.spcflags, (m), __ATOMIC_ACQ_REL);					\
	if (SPCFLAGS_TEST(SPCFLAG_STOP))			\
		SDL_CondSignal(stop_condition);			\
	SDL_UnlockMutex(spcflags_lock);	\
} while (0)

#define SPCFLAGS_CLEAR(m) do {				\
	SDL_LockMutex(spcflags_lock);		\
	__atomic_fetch_and(&regs.spcflags, ~(m), __ATOMIC_ACQ_REL);					\
	SDL_UnlockMutex(spcflags_lock);	\
} while (0)

#define SleepAndWait() do { \
	SDL_LockMutex(spcflags_lock);		\
	while (SPCFLAGS_TEST(SPCFLAG_STOP) && !cpu_stop_has_pending_work()) \
		SDL_CondWait(stop_condition, spcflags_lock); \
	SDL_UnlockMutex(spcflags_lock);	\
} while (0)

#endif

#endif /* SPCFLAGS_H */
