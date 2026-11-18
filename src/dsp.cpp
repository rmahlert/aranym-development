/*
 * DSP56001 emulation and ARAnyM host integration.
 * Copyright (C) 2001-2008 ARAnyM developer team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "sysdeps.h"
#include "hardware.h"
#include "cpu_emulation.h"
#include "memory-uae.h"
#include "dsp.h"
#include "peripheral_clock.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>
#if DSP_EMULATION
#include "dsp_core.h"
#include "dsp_cpu.h"
#endif
#include "dsp_host.h"
#define DEBUG 0
#include "debug.h"

extern "C" {
unsigned dsp_host_trace_mask;
}
static DSP *dsp_owner;

extern "C" int dsp_host_trace_enabled(unsigned mask)
{
	return (dsp_host_trace_mask & mask) != 0;
}
extern "C" void dsp_host_trace(unsigned mask, const char *format, ...)
{
	if (!(dsp_host_trace_mask & mask)) return;
	va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
}
extern "C" void dsp_host_warn(int, const char *format, ...)
{
	va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
}
extern "C" int dsp_host_debug_enabled(void)
{
#ifdef DEBUGGER
	return std::getenv("ARANYM_DSP_BREAK") != NULL;
#else
	return 0;
#endif
}
extern "C" void dsp_host_debugger(void)
{
#ifdef DEBUGGER
	activate_debugger();
#endif
}
extern "C" void dsp_host_ssi_receive_request(void)
{
	if (dsp_owner) dsp_owner->ssi_receive_request();
}
extern "C" void dsp_host_ssi_transmit_frame(uint32_t frame)
{
	if (dsp_owner) dsp_owner->ssi_transmit_request(frame);
}
#if DSP_EMULATION
static void dsp_host_irq(int asserted)
{
	TriggerDSP(asserted != 0);
}
#endif

DSP::DSP(memptr address, uint32 size) : BASE_IO(address, size),
	servicing(false), last_ns(0), clock_fraction(0), ssi_rx_request(NULL), ssi_tx_frame(NULL), ssi_opaque(NULL)
{
	dsp_owner = this;
	const char *trace = std::getenv("ARANYM_DSP_TRACE");
	dsp_host_trace_mask = trace ? static_cast<unsigned>(std::strtoul(trace, NULL, 0)) : 0;
#if DSP_EMULATION
	dsp_core_init(dsp_host_irq);
#endif
	reset();
}
DSP::~DSP()
{
#if DSP_EMULATION
	dsp_core_shutdown();
	TriggerDSP(false);
#endif
	if (dsp_owner == this) dsp_owner = NULL;
}
void DSP::reset()
{
	last_ns = peripheral_clock_now_ns();
	clock_fraction = 0;
#if DSP_EMULATION
	dsp_core_reset();
#endif
}
void DSP::service(uint64_t now_ns)
{
	if (servicing) return;
	if (now_ns < last_ns) {
		last_ns = now_ns;
		clock_fraction = 0;
#if DSP_EMULATION
		dsp_core.cycle_credit = 0;
#endif
		return;
	}
	const uint64_t elapsed = now_ns - last_ns;
	last_ns = now_ns;
#if DSP_EMULATION
	/* 32 MHz is exactly four oscillator clocks per 125 nanoseconds. */
	const uint32_t fraction = static_cast<uint32_t>((elapsed % 125) * 4) + clock_fraction;
	const uint64_t cycles = (elapsed / 125) * 4 + fraction / 125;
	clock_fraction = fraction % 125;
	servicing = true;
	dsp56k_run_cycles(cycles);
	servicing = false;
#endif
}
uint64_t DSP::next_deadline_ns() const
{
#if DSP_EMULATION
	if (!dsp_core.running ||
	    (dsp_core.halt_state == DSP_HALT_STOP && !dsp_core.stop_delay_cycles) ||
	    (dsp_core.halt_state == DSP_HALT_WAIT && !dsp56k_interrupt_pending()))
		return std::numeric_limits<uint64_t>::max();
	/* Device accesses synchronize exactly; schedule free-running work in batches. */
	return last_ns > std::numeric_limits<uint64_t>::max() - 32000 ?
		std::numeric_limits<uint64_t>::max() : last_ns + 32000;
#else
	return std::numeric_limits<uint64_t>::max();
#endif
}
void DSP::exec_insts(int count)
{
#if DSP_EMULATION
	while (count-- > 0 && dsp_core.running && dsp_core.halt_state == DSP_HALT_NONE)
		dsp56k_execute_instruction();
#else
	(void)count;
#endif
}
uint8 DSP::handleRead(memptr address)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	return dsp_core_read_host(static_cast<int>(address - getHWoffset()));
#else
	return 0xff;
#endif
}
uint16 DSP::handleReadW(memptr address)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	const int offset = static_cast<int>(address - getHWoffset());
	const uint16 high = dsp_core_read_host(offset);
	return (high << 8) | dsp_core_read_host(offset + 1);
#else
	(void)address;
	return 0xffff;
#endif
}
uint32 DSP::handleReadL(memptr address)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	const int offset = static_cast<int>(address - getHWoffset());
	uint32 value = 0;
	for (int byte = 0; byte < 4; ++byte)
		value = (value << 8) | dsp_core_read_host(offset + byte);
	return value;
#else
	(void)address;
	return 0xffffffffu;
#endif
}
void DSP::handleWrite(memptr address, uint8 value)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	dsp_core_write_host(static_cast<int>(address - getHWoffset()), value);
#else
	(void)address; (void)value;
#endif
}
void DSP::handleWriteW(memptr address, uint16 value)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	const int offset = static_cast<int>(address - getHWoffset());
	dsp_core_write_host(offset, value >> 8);
	dsp_core_write_host(offset + 1, value);
#else
	(void)address; (void)value;
#endif
}
void DSP::handleWriteL(memptr address, uint32 value)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	const int offset = static_cast<int>(address - getHWoffset());
	for (int byte = 0; byte < 4; ++byte)
		dsp_core_write_host(offset + byte, value >> (24 - byte * 8));
#else
	(void)address; (void)value;
#endif
}
bool DSP::host_irq_asserted() const
{
#if DSP_EMULATION
	return (dsp_core.hostport[CPU_HOST_ISR] & (1u << CPU_HOST_ISR_HREQ)) != 0;
#else
	return false;
#endif
}
bool DSP::ssi_transmit_slot_active() const
{
#if DSP_EMULATION
	return dsp_core.ssi.transmit_slot_active != 0;
#else
	return false;
#endif
}
int DSP::host_interrupt_acknowledge()
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	return host_irq_asserted() ? dsp_core.hostport[CPU_HOST_IVR] : -1;
#else
	return -1;
#endif
}
void DSP::set_irqa(bool asserted)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	dsp56k_set_irq_line(DSP_INTER_IRQA, asserted);
#else
	(void)asserted;
#endif
}
void DSP::set_irqb(bool asserted)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	dsp56k_set_irq_line(DSP_INTER_IRQB, asserted);
#else
	(void)asserted;
#endif
}
void DSP::set_nmi(bool asserted)
{
	service(peripheral_clock_event_time_ns());
#if DSP_EMULATION
	dsp56k_set_irq_line(DSP_INTER_NMI, asserted);
#else
	(void)asserted;
#endif
}
void DSP::ssi_receive_frame(uint32_t frame)
{
	ssi_receive_frame_at(frame, peripheral_clock_event_time_ns());
}
void DSP::ssi_receive_frame_at(uint32_t frame, uint64_t when)
{
	service(when);
#if DSP_EMULATION
	if (dsp_core.halt_state != DSP_HALT_STOP) dsp_core_ssi_Receive_SC1(frame);
#else
	(void)frame;
#endif
}
void DSP::ssi_receive_clock(uint32_t value)
{
	ssi_receive_clock_at(value, peripheral_clock_event_time_ns());
}
void DSP::ssi_receive_clock_at(uint32_t value, uint64_t when)
{
	service(when);
#if DSP_EMULATION
	if (dsp_core.halt_state != DSP_HALT_STOP) {
		dsp_core.ssi.received_value = value;
		dsp_core_ssi_Receive_SC0();
	}
#else
	(void)value;
#endif
}
void DSP::ssi_transmit_frame(uint32_t frame)
{
	ssi_transmit_frame_at(frame, peripheral_clock_event_time_ns());
}
void DSP::ssi_transmit_frame_at(uint32_t frame, uint64_t when)
{
	service(when);
#if DSP_EMULATION
	if (dsp_core.halt_state != DSP_HALT_STOP) dsp_core_ssi_Receive_SC2(frame);
#else
	(void)frame;
#endif
}
uint32_t DSP::ssi_transmit_clock()
{
	return ssi_transmit_clock_at(peripheral_clock_event_time_ns());
}
uint32_t DSP::ssi_transmit_clock_at(uint64_t when)
{
	service(when);
#if DSP_EMULATION
	if (dsp_core.halt_state != DSP_HALT_STOP) {
		dsp_core_ssi_Receive_SCK();
		return dsp_core.ssi.transmit_value;
	}
#endif
	return 0;
}
void DSP::set_ssi_callbacks(void (*rx)(void *), void (*tx)(void *, uint32_t), void *opaque)
{
	ssi_rx_request = rx; ssi_tx_frame = tx; ssi_opaque = opaque;
}
void DSP::ssi_receive_request()
{
	if (ssi_rx_request) ssi_rx_request(ssi_opaque);
}
void DSP::ssi_transmit_request(uint32_t frame)
{
	if (ssi_tx_frame) ssi_tx_frame(ssi_opaque, frame);
}
