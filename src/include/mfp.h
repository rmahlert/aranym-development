/*
 * mfp.h - MFP emulation - declaration
 *
 * Copyright (c) 2001-2005 Petr Stehlik of ARAnyM dev team (see AUTHORS)
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

#ifndef _MFP_H
#define _MFP_H

#include "icio.h"
#include "mfp_timer.h"

class MFP_TimerA:public MFP_Timer {
public:
	MFP_TimerA() : MFP_Timer(0) {};
};

class MFP_TimerB:public MFP_Timer {
public:
	MFP_TimerB() : MFP_Timer(1) {};
};

class MFP_TimerC:public MFP_Timer {
public:
	MFP_TimerC() : MFP_Timer(2) {};
};

class MFP_TimerD:public MFP_Timer {
public:
	MFP_TimerD() : MFP_Timer(3) {};
};

class USART {
public:
	uint8 synchar;
	uint8 control;
	uint8 rxstat;
	uint8 txstat;
	uint8 data;
};

/*****************************************************************/

class MFP : public BASE_IO {
private:
	uint8 input;
	uint8 GPIP_data;
	uint8 active_edge;
	uint8 data_direction;
	uint16 irq_enable;
	uint16 irq_pending;
	uint16 irq_inservice;
	uint16 irq_mask;
	MFP_TimerA A;
	MFP_TimerB B;
	MFP_TimerC C;
	MFP_TimerD D;
	// USART usart;
	int vr;
	bool timer_a_high, timer_b_high;

public:
	MFP(memptr addr, uint32 size);
	void reset();
	virtual uint8 handleRead(memptr);
	virtual void handleWrite(memptr, uint8);
	void IRQ(int, int count);
	void mcpSerialPoll();	// the MCP tools' serial input (ENABLE_MCP)
	void setGPIPbit(int mask, int value);
	void setGPIPbit_at(int mask, int value, std::uint64_t when_ns);
	int doInterrupt(void);
	int timerC_ms_ticks();
	void service(std::uint64_t now_ns);
	std::uint64_t next_deadline_ns() const;
	void timer_input(unsigned timer, bool active);
	void setTimerAInput(bool high, std::uint64_t when_ns);
	void setTimerBInput(bool high, std::uint64_t when_ns);
	bool irq_asserted() const;

private:
	void set_active_edge(uint8 value);
	void update_irq();
};

#endif /* _MFP_H */
