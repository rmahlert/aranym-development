/*
	Atari MIDI emulation

	ARAnyM (C) 2005-2006 Patrice Mandin

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _MIDI_H
#define _MIDI_H

#include "acia.h"
#include "host_midi.h"
#include "midi_acia6850.h"

class MIDI: public ACIA
{
	protected:
		virtual void close(void);
		MidiAcia6850 device;
		HostMidiTransport transport;
		HostMidiOptions host_options;
		uint64_t output_dropped;
		static void transmit_byte(void *, uint8_t, uint64_t);
		static void interrupt_changed(void *, bool);
		static void input_ready(void *);

	public:
		MIDI(memptr, uint32);
		virtual ~MIDI();
		virtual void reset(void);
		virtual void enable(bool bEnable);
		virtual const char *type() = 0;
		virtual uae_u8 handleRead(uaecptr addr);
		virtual void handleWrite(uaecptr addr, uae_u8 value);
		virtual uae_u8 ReadStatus();
		virtual void WriteControl(uae_u8 value);
		virtual uae_u8 ReadData();
		virtual void WriteData(uae_u8 value);
		void poll_host(uint64_t now_ns);
		void service(uint64_t now_ns, bool poll=true);
		uint64_t next_deadline_ns() const;
		bool receive_byte(uint8_t value, uint64_t start_ns, uint8_t errors=0);
		bool live_portmidi_input() const {
			return host_options.backend == "portmidi" && !host_options.input.empty() && transport.active();
		}
		bool host_active() const { return transport.active(); }
		std::string host_error() const { return transport.error(); }
		uint64_t input_drops() const { return device.input_drops(); }
		uint64_t output_drops() const { return output_dropped; }
};

#endif /* _MIDI_H */
