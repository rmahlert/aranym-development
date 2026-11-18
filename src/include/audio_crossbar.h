/*
	Audio crossbar emulation

	ARAnyM (C) 2005 Patrice Mandin

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

#ifndef CROSSBAR_H
#define CROSSBAR_H
#include "icio.h"
#include "SDL_compat.h"
#include <atomic>
#include <stdint.h>
#include <vector>

class CROSSBAR : public BASE_IO {
    struct Clock {
        uint64_t next, numerator, denominator, fraction;
        void configure(uint64_t now, uint64_t num, uint64_t den, bool enabled);
        void advance();
    } clocks[3];
    struct Frame { int16_t left, right; };
    static const unsigned QUEUE_SIZE = 4096;
    Frame pcm[QUEUE_SIZE];
    std::atomic<unsigned> pcm_read, pcm_write;
    SDL_AudioCVT conversion;
    std::vector<uint8> conversion_buffer;
    bool conversion_valid, servicing, playback_was_active;
    uint64_t last_ns;
    uint16 input, output, attenuation;
    uint8 extfreqdiv, intfreqdiv, rec_tracks, in_source, adc_input;
    uint8 gain, gpio_dir, gpio_data;
    unsigned dma_slot, dsp_slot, adc_slot, configured_tracks;
    bool rx_request, rx_master, tx_request;
    uint16 special_transfer;
    Frame dac, adc;
    static void audio_callback(void *, uint8 *, int);
    static void request_receive(void *);
    static void request_transmit(void *, uint32_t);
    void render(uint8 *, int);
    void clock_tick(unsigned clock, uint64_t when);
    void dma_transfer(uint64_t when);
    void dsp_transfer(uint64_t when);
    void route(unsigned source, int16_t value, unsigned slot, uint64_t when);
    void enqueue_output();
    unsigned source_clock(unsigned shift) const;
    bool source_needed(unsigned source) const;
public:
    CROSSBAR(memptr, uint32);
    ~CROSSBAR();
    void reset();
    virtual uae_u8 handleRead(uaecptr addr);
    virtual void handleWrite(uaecptr addr, uae_u8 value);
    void service(uint64_t now_ns);
    uint64_t next_deadline_ns() const;
    void configuration_changed(uint64_t now_ns);
    int getClockFreq();
    int getClockPrediv();
    // Emulator-thread input hooks; absent host inputs remain disconnected silence.
    void set_adc_samples(int16_t left, int16_t right);
    void external_word(int16_t value, unsigned slot, uint64_t when_ns);
};
#endif
