/* SPDX-License-Identifier: GPL-2.0-or-later
 * Same-CPU-thread notification before a hardware/backend writes guest RAM.
 * This invalidates only optional private frame provenance, never guest state.
 */
#ifndef ARANYM_MODERN040_FRAME_H
#define ARANYM_MODERN040_FRAME_H
#ifdef EXPERIMENTAL_UPSTREAM_MODERN040
void modern040_memory_barrier();
#else
static inline void modern040_memory_barrier() {}
#endif
#endif
