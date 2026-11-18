/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_PERIPHERAL_CLOCK_H
#define ARANYM_PERIPHERAL_CLOCK_H

#include <cstdint>

using PeripheralClockSource = std::uint64_t (*)(void *);

std::uint64_t peripheral_clock_now_ns();
std::uint64_t peripheral_clock_event_time_ns();
std::uint64_t peripheral_clock_last_event_ns();
void peripheral_clock_reset();
void peripheral_clock_service();
void peripheral_clock_reschedule();
void peripheral_clock_set_source(PeripheralClockSource source, void *opaque);
void peripheral_clock_start();
void peripheral_clock_shutdown();
extern unsigned peripheral_instruction_budget;

#endif
