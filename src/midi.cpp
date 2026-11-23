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

#include "sysdeps.h"
#include "hardware.h"
#include "cpu_emulation.h"
#include "memory-uae.h"
#include "midi.h"
#include "parameters.h"
#include "peripheral_clock.h"

#define DEBUG 0
#include "debug.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

MIDI::MIDI(memptr addr, uint32 size) : ACIA(addr, size),
	device(this,transmit_byte,interrupt_changed),output_dropped(0)
{
	D(bug("midi: interface created at 0x%06x", getHWoffset()));

	reset();
}

MIDI::~MIDI(void)
{
	D(bug("midi: interface destroyed at 0x%06x", getHWoffset()));
	close();
}

void MIDI::reset(void)
{
	D(bug("midi: reset"));

	const uint64_t now=peripheral_clock_now_ns();
	device.reset(now);
	transport.reset(now);
	output_dropped=0;
	set_acia_irq(true,false);
}

void MIDI::close(void)
{
	transport.close();
}

void MIDI::enable(bool enabled)
{
	bx_midi_options_t *options=&bx_options.midi;
	if (!enabled) {
		close();options->enabled=false;return;
	}
	HostMidiOptions requested;
	requested.backend=options->type;
	requested.input=options->input;
	requested.output=options->output;
	requested.append_output=options->append_output;
	requested.sequencer_device=options->sequencer_device;
	requested.wakeup=input_ready;
	if (requested.output.empty()) {
		if (requested.backend=="file" || requested.backend=="raw") requested.output=options->file;
		else if (requested.backend=="sequencer") requested.output=options->sequencer;
	}
	if (transport.active() && requested.backend==host_options.backend &&
		requested.input==host_options.input && requested.output==host_options.output &&
		requested.append_output==host_options.append_output &&
		requested.sequencer_device==host_options.sequencer_device) {
		options->enabled=true;return;
	}
	close();host_options=requested;
	options->enabled=transport.open_endpoints(requested,peripheral_clock_now_ns());
	if (!options->enabled && requested.backend!="none")
		panicbug("MIDI: %s",transport.error().c_str());
}

void MIDI::transmit_byte(void *opaque,uint8_t value,uint64_t time)
{
	MIDI *midi=static_cast<MIDI *>(opaque);
	if (midi->transport.active() && !midi->transport.submit_output(value,time))
		++midi->output_dropped;
}

void MIDI::interrupt_changed(void *,bool asserted)
{
	set_acia_irq(true,asserted);
}

void MIDI::input_ready(void *)
{
	TriggerInternalIRQ();
}

void MIDI::poll_host(uint64_t now)
{
	transport.service(now);
	const bool portmidi=host_options.backend=="portmidi";
	if (portmidi) device.resume_host_receive(now);
	HostMidiByte incoming;
	if (!device.can_receive() || !transport.poll_input(incoming)) return;
	const uint64_t delay=portmidi && now>incoming.timestamp_ns ? now-incoming.timestamp_ns : 0;
	unsigned int count=0;
	do {
		const uint64_t shift=std::min(delay,MidiAcia6850::never-incoming.timestamp_ns);
		device.queue_receive(incoming.byte,incoming.timestamp_ns+shift);
	} while (++count<256 && device.can_receive() && transport.poll_input(incoming));
}

void MIDI::service(uint64_t now, bool poll)
{
	if (poll) poll_host(now);
	device.advance(now);
}

uint64_t MIDI::next_deadline_ns() const
{
	return device.next_deadline_ns();
}

bool MIDI::receive_byte(uint8_t value,uint64_t start,uint8_t errors)
{
	return device.queue_receive(value,start,0x15,errors);
}

uae_u8 MIDI::ReadStatus()
{
	const uint64_t now=peripheral_clock_event_time_ns();
	return device.read_status(now);
}

void MIDI::WriteControl(uae_u8 value)
{
	const uint64_t now=peripheral_clock_event_time_ns();
	device.write_control(value,now);
}

uae_u8 MIDI::ReadData()
{
	const uint64_t now=peripheral_clock_event_time_ns();
	return device.read_data(now);
}

void MIDI::WriteData(uae_u8 value)
{
	const uint64_t now=peripheral_clock_event_time_ns();
	device.write_data(value,now);
}

uae_u8 MIDI::handleRead(uaecptr addr)
{
	switch (addr-getHWoffset()) {
	case 0:return ReadStatus();
	case 2:return ReadData();
	default:return 0;
	}
}

void MIDI::handleWrite(uaecptr addr,uae_u8 value)
{
	switch (addr-getHWoffset()) {
	case 0:WriteControl(value);break;
	case 2:WriteData(value);break;
	}
}
