/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit experimental entry points; never enabled by normal builds.
 */
#ifndef ARANYM_MODERN040_EXPERIMENTAL_ADAPTER_H
#define ARANYM_MODERN040_EXPERIMENTAL_ADAPTER_H
void modern040_execute();
void modern040_prepare();
void modern040_reset_requested();
void modern040_external_exception(int vector);
void modern040_external_interrupt(int vector,unsigned level);
#ifdef EXPERIMENTAL_UPSTREAM_MODERN040
bool modern040_restart_pending();
bool modern040_owns_exception();
bool modern040_owns_interrupt();
bool modern040_irq_restart_pending();
bool modern040_interrupt_ready(unsigned level);
#endif
#endif
