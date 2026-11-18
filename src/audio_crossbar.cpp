/*
	Audio Crossbar emulation

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
 
/* Falcon routing and handshake algorithms adapted from Hatari - Crossbar.c,
 * by Laurent Sallafranque, Nicolas Pomarede, Thomas Huth and the Hatari team.
 * Original license notice:
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */
#include "sysdeps.h"
#include "hardware.h"
#include "host.h"
#include "audio_crossbar.h"
#include "audio_dma.h"
#include "dsp.h"
#include "peripheral_clock.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace {
const uint64_t NEVER = std::numeric_limits<uint64_t>::max();
const uint16_t dac_volume[16] = {
    65535,55142,46396,39037,32846,27636,23253,19565,
    16462,13851,11654,9806,8250,6942,5841,4915
};
const uint16_t adc_volume[16] = {
    3276,3894,4628,5500,6537,7769,9234,10975,
    13043,15502,18424,21897,26025,30931,36761,43691
};
int16_t clip(int value) { return int16_t(std::max(-32768, std::min(32767, value))); }
}

void CROSSBAR::Clock::configure(uint64_t now, uint64_t num, uint64_t den, bool enabled)
{
    if (!enabled || !den) { next = NEVER; numerator = num; denominator = den; return; }
    if (next != NEVER && numerator == num && denominator == den) return;
    numerator = num; denominator = den; fraction = den - 1; next = now;
    advance();
}
void CROSSBAR::Clock::advance()
{
    const uint64_t total = numerator + fraction;
    const uint64_t delay = total / denominator;
    fraction = total % denominator;
    next = next > NEVER - delay ? NEVER : next + delay;
}

CROSSBAR::CROSSBAR(memptr addr, uint32 size) : BASE_IO(addr, size),
    pcm_read(0), pcm_write(0), conversion_valid(false), servicing(false)
{
    std::memset(&conversion, 0, sizeof(conversion));
    if (host && host->audio.obtained.freq > 0 && host->audio.obtained.channels > 0 &&
        SDL_BuildAudioCVT(&conversion, AUDIO_S16SYS, 2, host->audio.obtained.freq,
            host->audio.obtained.format, host->audio.obtained.channels,
            host->audio.obtained.freq) >= 0) {
        conversion_valid = true;
        conversion_buffer.resize(512 * sizeof(Frame) * std::max(1, conversion.len_mult));
    }
    reset();
    if (getDSP()) getDSP()->set_ssi_callbacks(request_receive, request_transmit, this);
    if (host) host->audio.AddCallback(audio_callback, this);
}
CROSSBAR::~CROSSBAR()
{
    // DSP is destroyed before the crossbar by the hardware owner.
    if (host) host->audio.RemoveCallback(audio_callback);
}
void CROSSBAR::reset()
{
    input = output = attenuation = 0;
    extfreqdiv = intfreqdiv = rec_tracks = in_source = adc_input = gain = gpio_dir = gpio_data = 0;
    dma_slot = dsp_slot = adc_slot = 0;
    configured_tracks = 1;
    rx_request = rx_master = tx_request = playback_was_active = false;
    special_transfer = 0;
    dac = adc = Frame();
    last_ns = peripheral_clock_now_ns();
    for (unsigned i = 0; i < 3; ++i) { clocks[i] = Clock(); clocks[i].next = NEVER; }
    SDL_LockAudio();
    pcm_read.store(0); pcm_write.store(0);
    SDL_UnlockAudio();
}
unsigned CROSSBAR::source_clock(unsigned shift) const
{
    if (!intfreqdiv) return 0; // STE mode uses the 25MHz domain for every source.
    return (output >> shift) & 3;
}
bool CROSSBAR::source_needed(unsigned source) const
{
    return ((in_source & 2) && ((input >> 13) & 3) == source) ||
        ((input & 0x80) && ((input >> 5) & 3) == source) ||
        (getAUDIODMA()->recording_active() && ((input >> 1) & 3) == source);
}
void CROSSBAR::configuration_changed(uint64_t now)
{
    const unsigned tracks = getAUDIODMA()->playback_tracks();
    if (tracks != configured_tracks) {
        dma_slot = dsp_slot = 0;
        configured_tracks = tracks;
    }
    const bool playing = getAUDIODMA()->playback_active();
    if (playing && !playback_was_active) dma_slot = 0;
    playback_was_active = playing;
    const bool dma_needed = playing;
    const bool dsp_needed = (output & 0x80) && source_needed(1);
    const bool adc_needed = source_needed(3) || (in_source & 1);
    const bool run25 = (dma_needed && source_clock(1) == 0) ||
        (dsp_needed && source_clock(5) == 0) || adc_needed;
    const bool run32 = intfreqdiv && ((dma_needed && source_clock(1) == 2) ||
        (dsp_needed && source_clock(5) == 2));
    uint64_t numerator;
    uint64_t denominator25, denominator32;
    if (!intfreqdiv) {
        // STE master clock / (1280,640,320,160), preserving fractional clocks.
        numerator = uint64_t(1000000000) * (1280u >> getAUDIODMA()->legacy_frequency());
        denominator25 = uint64_t(8010613) * tracks * 2;
        denominator32 = 1;
    } else {
        numerator = uint64_t(1000000000) * 256 * (intfreqdiv + 1);
        denominator25 = uint64_t(25175040) * tracks * 2;
        denominator32 = uint64_t(32000000) * tracks * 2;
    }
    clocks[0].configure(now, numerator, denominator25, run25);
    clocks[1].configure(now, numerator, denominator32, run32);
    // Host rendering samples the held codec output independently of source
    // clocks. Stopping DMA must not discard its final stereo pair.
    clocks[2].configure(now, 1000000000, conversion_valid ? host->audio.obtained.freq : 0, in_source != 0);
}

void CROSSBAR::request_receive(void *opaque)
{
    CROSSBAR *self = static_cast<CROSSBAR *>(opaque);
    self->rx_request = true;
    self->rx_master = true;
}
void CROSSBAR::request_transmit(void *opaque, uint32_t frame)
{
    static_cast<CROSSBAR *>(opaque)->tx_request = frame != 0;
}
void CROSSBAR::dma_transfer(uint64_t when)
{
    const bool handshake = (input & 0xf0) == 0x80;
    if (handshake && rx_master && !rx_request) return;
    if (getAUDIODMA()->playback_frame_start()) dma_slot = 0;
    int16_t sample;
    if (!getAUDIODMA()->playback_word(dma_slot, sample, when)) return;
    if (handshake && rx_master && source_clock(1) == 2) {
        const uint16_t previous = special_transfer;
        special_transfer = uint16_t(sample);
        sample = int16_t((previous << 2) | ((uint16_t(sample) >> 14) & 3));
    }
    route(0, sample, dma_slot, when);
    dma_slot = (dma_slot + 1) % (configured_tracks * 2);
}
void CROSSBAR::dsp_transfer(uint64_t when)
{
    if (!(output & 0x80) || !source_needed(1)) return;
    const bool record_handshake = (input & 15) == 2;
    if (record_handshake && (!getAUDIODMA()->recording_active() || !tx_request)) return;
    if (!record_handshake) getDSP()->ssi_transmit_frame_at(dsp_slot == 0, when);
    const uint32_t sample = getDSP()->ssi_transmit_clock_at(when);
    if (record_handshake) tx_request = false;
    if (getDSP()->ssi_transmit_slot_active()) route(1, int16_t(sample), dsp_slot, when);
    dsp_slot = (dsp_slot + 1) % (configured_tracks * 2);
}
void CROSSBAR::route(unsigned source, int16_t value, unsigned slot, uint64_t when)
{
    if (((input >> 1) & 3) == source)
        getAUDIODMA()->record_word(value, slot, rec_tracks + 1, when);
    if ((input & 0x80) && ((input >> 5) & 3) == source) {
        // A DSP-generated SC1 request already supplied its frame synchronization.
        if (!rx_master || !rx_request) getDSP()->ssi_receive_frame_at(slot == 0, when);
        rx_request = false;
        getDSP()->ssi_receive_clock_at(uint32_t(int32_t(value)), when);
    }
    if (((input >> 13) & 3) == source && slot / 2 == getAUDIODMA()->monitor_track()) {
        const int scaled = source == 0 && getAUDIODMA()->eight_bit() ? int(value) * 256 : value;
        if (slot & 1) dac.right = int16_t(scaled); else dac.left = int16_t(scaled);
    }
}
void CROSSBAR::clock_tick(unsigned clock, uint64_t when)
{
    if (source_clock(5) == clock) dsp_transfer(when);
    if (source_clock(1) == clock) dma_transfer(when);
    if (clock == 0 && (source_needed(3) || (in_source & 1))) {
        // No host capture/PSG adapter is installed here; only supplied ADC samples are routed.
        const int16_t sample = adc_slot & 1 ? ((adc_input & 1) ? 0 : adc.right) :
            ((adc_input & 2) ? 0 : adc.left);
        route(3, sample, adc_slot, when);
        adc_slot ^= 1;
    }
}
void CROSSBAR::enqueue_output()
{
    int left = in_source & 2 ? dac.left : 0;
    int right = in_source & 2 ? dac.right : 0;
    if (in_source & 1) {
        if (!(adc_input & 2)) left += (int(adc.left) * adc_volume[gain >> 4]) / 16384;
        if (!(adc_input & 1)) right += (int(adc.right) * adc_volume[gain & 15]) / 16384;
    }
    left = int(int64_t(left) * dac_volume[(attenuation >> 8) & 15] / 65535);
    right = int(int64_t(right) * dac_volume[(attenuation >> 4) & 15] / 65535);
    const unsigned write = pcm_write.load(std::memory_order_relaxed);
    const unsigned next = (write + 1) % QUEUE_SIZE;
    if (next == pcm_read.load(std::memory_order_acquire)) return;
    pcm[write] = Frame{clip(left), clip(right)};
    pcm_write.store(next, std::memory_order_release);
}
void CROSSBAR::service(uint64_t now)
{
    if (servicing) return;
    servicing = true;
    if (now < last_ns) {
        for (unsigned i = 0; i < 3; ++i) clocks[i].next = NEVER;
    }
    configuration_changed(last_ns = now);
    for (;;) {
        const uint64_t next = next_deadline_ns();
        if (next == NEVER || next > now) break;
        getDSP()->service(next);
        for (unsigned i = 0; i < 3; ++i) {
            if (clocks[i].next != next) continue;
            clocks[i].advance();
            if (i == 2) enqueue_output(); else clock_tick(i == 1 ? 2 : 0, next);
        }
        configuration_changed(next);
    }
    servicing = false;
}
uint64_t CROSSBAR::next_deadline_ns() const
{
    return std::min(clocks[0].next, std::min(clocks[1].next, clocks[2].next));
}
void CROSSBAR::set_adc_samples(int16_t left, int16_t right)
{
    peripheral_clock_event_time_ns();
    adc = Frame{left, right};
}
void CROSSBAR::external_word(int16_t value, unsigned slot, uint64_t when)
{
    service(when);
    getDSP()->service(when);
    if (source_clock(5) == 1) dsp_transfer(when);
    if (source_clock(1) == 1) dma_transfer(when);
    route(2, value, slot, when);
}

uae_u8 CROSSBAR::handleRead(uaecptr addr)
{
    peripheral_clock_event_time_ns();
    switch (addr - getHWoffset()) {
    case 0: return output >> 8; case 1: return output;
    case 2: return input >> 8; case 3: return input;
    case 4: return extfreqdiv; case 5: return intfreqdiv;
    case 6: return rec_tracks; case 7: return in_source;
    case 8: return adc_input; case 9: return gain;
    case 10: return attenuation >> 8; case 11: return attenuation;
    case 17: return gpio_dir; case 19: return gpio_data;
    default: return 0;
    }
}
void CROSSBAR::handleWrite(uaecptr addr, uae_u8 value)
{
    const uint64_t now = peripheral_clock_event_time_ns();
    switch (addr - getHWoffset()) {
    case 0: output = (output & 255) | (uint16(value) << 8); break;
    case 1: output = (output & 0xff00) | value; break;
    case 2: input = (input & 255) | (uint16(value) << 8); break;
    case 3:
        input = (input & 0xff00) | value;
        rx_request = (input & 0xf0) == 0x80;
        rx_master = false;
        break;
    case 4: extfreqdiv = value & 15; break;
    case 5: intfreqdiv = value & 15; break;
    case 6: rec_tracks = value & 3; break;
    case 7: in_source = value & 3; break;
    case 8: adc_input = value & 3; break;
    case 9: gain = value; break;
    case 10: attenuation = (attenuation & 0x00f0) | (uint16(value & 15) << 8); break;
    case 11: attenuation = (attenuation & 0x0f00) | (value & 0xf0); break;
    case 17: gpio_dir = value & 7; break;
    case 19: gpio_data = value & 7; break;
    default: return;
    }
    configuration_changed(now);
}
int CROSSBAR::getClockFreq()
{
    const unsigned clock = source_clock(1);
    return clock == 0 ? 25175040 : clock == 2 ? 32000000 : 0;
}
int CROSSBAR::getClockPrediv()
{
    return source_clock(1) == 1 ? extfreqdiv : intfreqdiv;
}
void CROSSBAR::audio_callback(void *opaque, uint8 *stream, int length)
{
    static_cast<CROSSBAR *>(opaque)->render(stream, length);
}
void CROSSBAR::render(uint8 *stream, int length)
{
    if (!conversion_valid) return;
    const unsigned frame_bytes = (host->audio.obtained.format & 255) / 8 * host->audio.obtained.channels;
    if (!frame_bytes) return;
    while (length >= int(frame_bytes)) {
        const unsigned wanted = std::min(512u, unsigned(length) / frame_bytes);
        unsigned read = pcm_read.load(std::memory_order_relaxed);
        const unsigned write = pcm_write.load(std::memory_order_acquire);
        unsigned count = 0;
        while (count < wanted && read != write) {
            std::memcpy(&conversion_buffer[count * sizeof(Frame)], &pcm[read], sizeof(Frame));
            read = (read + 1) % QUEUE_SIZE;
            ++count;
        }
        pcm_read.store(read, std::memory_order_release);
        if (!count) break;
        conversion.buf = conversion_buffer.data();
        conversion.len = count * sizeof(Frame);
        if (SDL_ConvertAudio(&conversion) < 0) break;
        const int converted = std::min(length, conversion.len_cvt);
#if SDL_VERSION_ATLEAST(2, 0, 0)
        SDL_MixAudioFormat(stream, conversion.buf, host->audio.obtained.format, converted, SDL_MIX_MAXVOLUME);
#else
        SDL_MixAudio(stream, conversion.buf, converted, SDL_MIX_MAXVOLUME);
#endif
        stream += converted; length -= converted;
    }
}
