/*
 * DSP56001 emulation and ARAnyM host integration.
 * Copyright (C) 2001-2008 ARAnyM developer team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DSP_H
#define DSP_H
#include "icio.h"
#include <stdint.h>

class DSP : public BASE_IO {
public:
	DSP(memptr address, uint32 size);
	~DSP();
	virtual void reset(void);
	virtual uint8 handleRead(memptr address);
	virtual uint16 handleReadW(memptr address);
	virtual uint32 handleReadL(memptr address);
	virtual void handleWrite(memptr address, uint8 value);
	virtual void handleWriteW(memptr address, uint16 value);
	virtual void handleWriteL(memptr address, uint32 value);
	void exec_insts(int count);
	void service(uint64_t now_ns);
	uint64_t next_deadline_ns() const;
	bool host_irq_asserted() const;
	bool ssi_transmit_slot_active() const;
	int host_interrupt_acknowledge();
	void set_irqa(bool asserted);
	void set_irqb(bool asserted);
	void set_nmi(bool asserted);

	void ssi_receive_frame(uint32_t frame);
	void ssi_receive_frame_at(uint32_t frame, uint64_t when);
	void ssi_receive_clock_at(uint32_t value, uint64_t when);
	void ssi_transmit_frame_at(uint32_t frame, uint64_t when);
	uint32_t ssi_transmit_clock_at(uint64_t when);
	void ssi_receive_clock(uint32_t value);
	void ssi_transmit_frame(uint32_t frame);
	uint32_t ssi_transmit_clock();
	void set_ssi_callbacks(void (*receive_request)(void *),
		void (*transmit_frame)(void *, uint32_t), void *opaque);
	void ssi_receive_request();
	void ssi_transmit_request(uint32_t frame);
private:
	bool servicing;
	uint64_t last_ns;
	uint32_t clock_fraction;
	void (*ssi_rx_request)(void *);
	void (*ssi_tx_frame)(void *, uint32_t);
	void *ssi_opaque;
};
#endif
