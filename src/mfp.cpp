/*
 * mfp.cpp - MFP emulation
 *
 * Copyright (c) 2001-2005 Petr Stehlik of ARAnyM dev team (see AUTHORS)
 *
 * Copied almost bit-by-bit from STonC's mfp.c (thanks, Laurent!)
 *
 * This file is part of the ARAnyM project which builds a new and powerful
 * TOS/FreeMiNT compatible virtual machine running on almost any hardware.
 *
 * ARAnyM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ARAnyM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ARAnyM; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"
#include "hardware.h"
#include "cpu_emulation.h"
#include "memory-uae.h"
#include "mfp.h"
#include "peripheral_clock.h"
#include <algorithm>
#include <limits>

#define DEBUG 0
#include "debug.h"

/*************************************************************************/

MFP::MFP(memptr addr, uint32 size) : BASE_IO(addr, size)
{
	reset();
}

#define MFP_VR_VECTOR 0xF0 
#define MFP_VR_SEI 0x08         /* Software end of interrupt, 
                                need software cancelling in inservice ? */
#define MFP_VR_AEI 0x00         /* Automatic end of interrupt,
                                ??? */
void MFP::reset()
{
	input = 0xff;
	GPIP_data = input;
	vr = 0x0100;
	active_edge = 0;
	data_direction = 0;
	irq_enable = 0;
	irq_pending = 0;
	irq_inservice = 0;
	irq_mask = 0;
	timer_a_high = timer_b_high = true;

	const auto now = peripheral_clock_now_ns();
	A.reset(now);
	B.reset(now);
	C.reset(now);
	D.reset(now);
	update_irq();
}

uint8 MFP::handleRead(memptr addr)
{
	service(peripheral_clock_event_time_ns());
	addr -= getHWoffset();
	if (addr > getHWsize())
		return 0;	// unhandled

	uint8 value;
	switch(addr) {
		case 0x01:	{
						Parallel *parallel = getYAMAHA()->parallel;
						value = (GPIP_data & ~ 0x21) | (parallel && parallel->Enabled() ? parallel->getBusy() : 0x01);
					}
					break;

		case 0x03:	value = active_edge;
					break;

		case 0x05:	value = data_direction;
					break;

		case 0x07:	value = irq_enable >> 8;
					break;

		case 0x09:	value = irq_enable;
					break;

		case 0x0b:	value = irq_pending >> 8;
					break;

		case 0x0d:	// D(bug("Read: TimerC IRQ %s pending", (irq_pending & 0x20) ? "" : "NOT"));
					value = irq_pending;
					break;

		case 0xf:	value = irq_inservice >> 8;
					break;

		case 0x11:	// D(bug("Read: TimerC IRQ %s in-service", (irq_inservice & 0x20) ? "" : "NOT"));
					value = irq_inservice;
					break;
						
		case 0x13:	value = irq_mask >> 8;
					break;
						
		case 0x15:	// D(bug("Read: TimerC IRQ %s masked", (irq_mask & 0x20) ? "" : "NOT"));
					value = irq_mask;
					break;
						
		case 0x17:	value = vr;
					break;

		case 0x19:	value = A.getControl();
					break;

		case 0x1b:	value = B.getControl();
					break;

		case 0x1d:	value = (C.getControl() << 4) | D.getControl();
					break;

		case 0x1f:	value = A.getData();
					break;

		case 0x21:	value = B.getData();
					break;

		case 0x23:	value = C.getData();
					break;
						
		case 0x25:	value = D.getData();
					break;
						
		case 0x2d:	value = 0x80;	// for Linux/m68k
					break;

		default: value = 0;
	};
	D(bug("Reading MFP data from %04lx = %d ($%02x) at %06x", addr, value, value, showPC()));
	return value;
}

void MFP::set_active_edge(uint8 value)
{
static int map_gpip_to_ier[8] = {0, 1, 2, 3, 6, 7, 14, 15};	
  /* AER : 1=Rising (0->1), 0=Falling (1->0) */
  
  /* [mfp.txt]
   * The edge bit is simply one input to an exclusive-or
   * gate, with the other input coming from the input buffer and the output going
   * to a 1-0 transition detector. Thus, depending upon the state of the input,
   * writing the AER can cause an interrupt-producing transition
   */
  /* this means : a = input xor aer, and interrupt when a 1->0.
   *  ____before____ ______after_____ ___result___
   *  input  aer  a   input  val  a    interrupt
   *    0     0   0     0     1   1      
   *    1     0   1     1     1   0       yes
   *    0     1   1     0     0   0       yes
   *    1     1   0     1     0   1      
   */
	int i, j;
	for(j = 0, i = 1; j < 8; j++, i <<= 1) {
		if( ((active_edge & i) != (value & i))        /* if AER changes */
				&& (! (data_direction & i))               /* for input lines */
				&& ((active_edge & i) != (input & i))) {
			IRQ(map_gpip_to_ier[j], 1);
		}
	}
 
	active_edge = value;
	if (A.set_input(timer_a_high == bool(active_edge & 0x10))) IRQ(13, 1);
	if (B.set_input(timer_b_high == bool(active_edge & 0x08))) IRQ(8, 1);
}

void MFP::handleWrite(memptr addr, uint8 value) {
	service(peripheral_clock_event_time_ns());
	addr -= getHWoffset();
	if (addr > getHWsize())
		return;	// unhandled

	D(bug("Writing MFP data to %04lx = %d ($%02x) at %06x", addr, value, value, showPC()));
	switch(addr) {
		case 0x01:	//GPIP_data = value;
					GPIP_data &= ~data_direction;
					GPIP_data |= value & data_direction;
					D(bug("New GPIP=$%x from PC=$%x", GPIP_data, showPC()));
					break;

		case 0x03:	set_active_edge(value);
					break;

		case 0x05:	data_direction = value;
					GPIP_data &= data_direction;
					GPIP_data |= input & (~data_direction);
					D(bug("GPIP Data Direction set to %02x", value));
					break;

		case 0x07:	irq_enable = (irq_enable & 0x00ff) | (value << 8);
					// cancel any pending interrupts, but do not alter ISR
					irq_pending &= irq_enable;
					// mfp_check_timers_ier();
					break;

		case 0x09:
#ifdef DEBUG_IER
					if ((irq_enable ^ value) & 0x20) {
						D(bug("Write: TimerC IRQ %sabled", (value & 20) ? "en" : "dis"));
					}
					if ((irq_enable ^ value) & 0x40) {
						D(bug("Write: IKBD IRQ %sabled", (value & 40) ? "en" : "dis"));
					}
#endif /* DEBUG */
					irq_enable = (irq_enable & 0xff00) | value;
					// cancel any pending interrupts, but do not alter ISR
					irq_pending &= irq_enable;
					// mfp_check_timers_ier();
					break;

  /* [mfp.txt]
   * IPRA and IPRB are also writeable and a pending interrupt can be cleared
   * without going through the acknowledge sequence by writing a zero to the
   * appropriate bit. This allows any one bit to be cleared, without altering any
   * other bits, simply by writing all ones except for the bit position to be
   * cleared on IPRA or IPRB.
   */
 
		case 0x0b:	irq_pending &= (value << 8) | 0xff;
  /* if no unmasked interrupt pending any more, there's no point in 
   * keeping F_MFP active 
   */
				if ((irq_pending & irq_mask) == 0) {
					TriggerMFP(false);
				}
				break;

		case 0x0d:
#ifdef DEBUG_IPR
					if ((irq_pending ^ value) & 0x20) {
						D(bug("Write: TimerC IRQ %s pending", (value & 20) ? "" : "NOT"));
					}
#endif /* DEBUG */
					irq_pending &= 0xff00 | value;
				if ((irq_pending & irq_mask) == 0) {
					TriggerMFP(false);
				}

					break;
 
  /* [mfp.txt]
   * Only a zero may be written into any bit of ISRA and ISRB; thus the 
   * in-service may be cleared in software but cannot be set in software.
   */
		case 0xf:	irq_inservice &= (value << 8) | 0xff;
					if (irq_pending & irq_mask) {
						// unmasked interrupt pending, signal it to cpu
						TriggerMFP(true);
					}
					break;

		case 0x11:
#ifdef DEBUG_ISR
					if ((irq_inservice ^ value) & 0x20) {
						D(bug("Write: TimerC IRQ %s in-service at %08x", (value & 20) ? "" : "NOT", showPC()));
					}
#endif /* DEBUG */
					irq_inservice &= 0xff00 | value;
					if (irq_pending & irq_mask) {
						// unmasked interrupt pending, signal it to cpu
						TriggerMFP(true);
					}
					break;
					
		case 0x13:	irq_mask = (irq_mask & 0x00ff) | (value << 8);
					TriggerMFP(irq_pending & irq_mask);
					break;
					
		case 0x15:
#ifdef DEBUG_IMR
					if ((irq_mask ^ value) & 0x20) {
						D(bug("Write: TimerC IRQ %s masked", (value & 20) ? "" : "NOT"));
					}
#endif /* DEBUG */
					irq_mask = (irq_mask & 0xff00) | value;
					TriggerMFP(irq_pending & irq_mask);
					break;
					
		case 0x17:	vr = value;
					if (vr & MFP_VR_SEI) {
						// software end-of-interrupt mode
					}
					else {
						// automatic end-of-interrupt mode : reset inservice bits?
						irq_inservice = 0;
						// try to pass a vector
						TriggerMFP(irq_pending & irq_mask);
					}
					break;

		case 0x19:	A.setControl(value);
					break;

		case 0x1b:	B.setControl(value);
					break;

		case 0x1d:	C.setControl((value >> 4) & 7);
					D.setControl(value & 7);
					break;

		case 0x1f:	A.setData(value);
					break;

		case 0x21:	B.setData(value);
					break;

		case 0x23:	C.setData(value);
					break;
					
		case 0x25:	D.setData(value);
					break;
					
		case 0x27:
		case 0x29:
		case 0x2b:
		case 0x2d:
		case 0x2f:
		default:
			break;
	};
	update_irq();
}

/*
 * setGPIPbit sets input port bits (peripheral interrupt request)
 */
void MFP::setGPIPbit(int mask, int value)
{
	setGPIPbit_at(mask, value, peripheral_clock_event_time_ns());
}

void MFP::setGPIPbit_at(int mask, int value, std::uint64_t when_ns)
{
	service(when_ns);
	static int map_gpip_to_ier[8] = {0, 1, 2, 3, 6, 7, 14, 15};	
	mask &= 0xff;

	int oldGPIP = GPIP_data;
	input &= ~mask;
	input |= (value & mask);
	/* if output port, no interrupt */
	mask &= ~data_direction;

	GPIP_data &= ~mask;
	GPIP_data |= (value & mask);
	GPIP_data &= 0xFF;
	D(bug("setGPIPbit($%x, $%x): old=$%x, new=$%x", mask, value, oldGPIP, GPIP_data));
	int i, j;
	for(j = 0, i = 1; j < 8; j++, i <<= 1) {
		if ((oldGPIP & i) != (GPIP_data & i)) {
			D(bug("setGPIPbit: i=$%x, irq_enable=$%x, old=$%x, new=$%x", i, irq_enable, oldGPIP, GPIP_data));
			if (active_edge & i) {
				/* interrupt when going from 0 to 1  */
				if (oldGPIP & i)
					continue;
			}
			else {
				/* interrupt when going from 1 to 0  */
				if (GPIP_data & i)
					continue;
			}
			D(bug("calling IRQ(%d)->%d", j, map_gpip_to_ier[j]));
			IRQ(map_gpip_to_ier[j], 1);
		}
	}	
}

/*
        Ask for an interrupt. Depending on diverse factors, an
        interrupt will eventually occur the next time 
        MFP:doInterrupt() is called. 

        0:  IO port 0 : Printer Busy
        1:  IO port 1 : Printer Acknowledge (RS232 Carrier detect on ST)
        2:  IO port 2 : RS232 CTS
        3:  IO port 3 : Blitter (DSP HREQ has a direct CPU IPL6 line)
        4:  timer D
        5:  timer C
        6:  IO port 4 : ACIA (IKBD/MIDI)
        7:  IO port 5 : FDC/SCSI/IDE
        8:  Timer B   : Display Enable
        9:  Send error
        10: Send buffer empty
        11: Receive error
        12: Receive buffer full
        13: Timer A   : DMA Sound Interrupt
        14: IO port 6 : RS232 ring
        15: IO port 7 : DMA Sound Interrupt (monochrome detect on ST)
*/

void MFP::IRQ(int int_level, int count)
{
    if (int_level < 0 || int_level > 15 || count <= 0)
        return;
    const unsigned bit = 1u << int_level;
    if (irq_enable & bit)
        irq_pending |= bit;
    update_irq();
}

bool MFP::irq_asserted() const
{
    const unsigned pending = irq_pending & irq_mask & irq_enable;
    for (unsigned bit = 0x8000; bit; bit >>= 1) {
        if (irq_inservice & bit)
            return false;
        if (pending & bit)
            return true;
    }
    return false;
}

void MFP::update_irq()
{
    TriggerMFP(irq_asserted());
}

int MFP::doInterrupt()
{
    service(peripheral_clock_event_time_ns());
    const unsigned pending = irq_pending & irq_mask & irq_enable;
    for (int level = 15; level >= 0; --level) {
        const unsigned bit = 1u << level;
        if (irq_inservice & bit)
            break;
        if (!(pending & bit))
            continue;
        irq_pending &= ~bit;
        if (vr & MFP_VR_SEI)
            irq_inservice |= bit;
        update_irq();
        return (vr & MFP_VR_VECTOR) | level;
    }
    update_irq();
    return 0;
}

void MFP::service(std::uint64_t now_ns)
{
    if (A.advance_to(now_ns)) IRQ(13, 1);
    if (B.advance_to(now_ns)) IRQ(8, 1);
    if (C.advance_to(now_ns)) IRQ(5, 1);
    if (D.advance_to(now_ns)) IRQ(4, 1);
}

std::uint64_t MFP::next_deadline_ns() const
{
    auto deadline = std::numeric_limits<std::uint64_t>::max();
    unsigned eligible = irq_enable & irq_mask & ~irq_pending;
    for (unsigned bit = 0x8000; bit; bit >>= 1) {
        if (irq_inservice & bit) {
            eligible &= ~((bit << 1) - 1);
            break;
        }
    }
    if (eligible & (1u << 13)) deadline = std::min(deadline, A.next_deadline_ns());
    if (eligible & (1u << 8)) deadline = std::min(deadline, B.next_deadline_ns());
    if (eligible & (1u << 5)) deadline = std::min(deadline, C.next_deadline_ns());
    if (eligible & (1u << 4)) deadline = std::min(deadline, D.next_deadline_ns());
    return deadline;
}

void MFP::timer_input(unsigned timer, bool active)
{
    service(peripheral_clock_event_time_ns());
    if (timer == 0 && A.set_input(active)) IRQ(13, 1);
    if (timer == 1 && B.set_input(active)) IRQ(8, 1);
}

int MFP::timerC_ms_ticks()
{
    const int frequency = C.compute_timer_freq();
    return frequency ? std::max(1, (1000 + frequency - 1) / frequency) : 5;
}

void MFP::setTimerAInput(bool high, std::uint64_t when_ns)
{
    service(when_ns);
    timer_a_high = high;
    if (A.set_input(high == bool(active_edge & 0x10))) IRQ(13, 1);
}

void MFP::setTimerBInput(bool high, std::uint64_t when_ns)
{
    service(when_ns);
    timer_b_high = high;
    if (B.set_input(high == bool(active_edge & 0x08))) IRQ(8, 1);
}
