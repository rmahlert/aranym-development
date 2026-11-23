/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_MIDI_ACIA6850_H
#define ARANYM_MIDI_ACIA6850_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

class MidiAcia6850 {
public:
    static constexpr uint64_t never = std::numeric_limits<uint64_t>::max();
    enum StatusBits : uint8_t {
        receive_full=0x01, transmit_empty=0x02, carrier_detect=0x04,
        clear_to_send=0x08, framing_error=0x10, overrun=0x20,
        parity_error=0x40, interrupt=0x80
    };
    using Transmit = void (*)(void *, uint8_t, uint64_t);
    using Interrupt = void (*)(void *, bool);

    explicit MidiAcia6850(void *context=nullptr, Transmit transmit=nullptr,
                         Interrupt irq=nullptr);
    void reset(uint64_t now_ns);
    void advance(uint64_t now_ns);
    uint8_t read_status(uint64_t now_ns);
    uint8_t read_data(uint64_t now_ns);
    void write_control(uint8_t value, uint64_t now_ns);
    void write_data(uint8_t value, uint64_t now_ns);
    uint64_t next_deadline_ns() const;

    bool queue_receive(uint8_t value, uint64_t start_ns,
                       uint8_t wire_format=0x15, uint8_t errors=0);
    void resume_host_receive(uint64_t now_ns);
    void receive_line(bool high, uint64_t now_ns);
    void modem_lines(bool cts_high, bool dcd_high, uint64_t now_ns);
    bool irq() const { return irq_asserted_; }
    bool transmit_line() const { return tx_line_; }
    bool rts_high() const;
    uint8_t control() const { return control_; }
    uint64_t time_ns() const { return now_; }
    uint64_t input_drops() const { return input_drops_; }
    uint64_t late_inputs() const { return late_inputs_; }
    bool can_receive() const { return input_count_<input_capacity; }

private:
    struct Format { uint8_t bits, parity, stops; };
    enum class RxPhase { idle, start, active };
    struct InputFrame { uint64_t start; uint8_t value, format, errors; };
    static constexpr size_t input_capacity=1024;
    static Format format(uint8_t control);
    static uint64_t bit_period(uint8_t control);
    static uint64_t add_time(uint64_t value, uint64_t delta);
    uint64_t next_tx_edge(uint64_t now) const;
    void clear_device(uint64_t now, uint8_t control);
    void update_irq();
    void set_receive_line(bool high, uint64_t now);
    void sample_receive();
    void receive_complete();
    void transmit_tick();
    void begin_transmit();
    void wire_tick();

    void *context_;
    Transmit transmit_;
    Interrupt interrupt_;
    uint64_t now_=0, tx_origin_=0, tx_next_=never, rx_next_=never;
    uint8_t control_=3, status_=transmit_empty, tdr_=0, rdr_=0;
    uint8_t tx_shift_=0, tx_value_=0, tx_bits_=0, tx_parity_=0, tx_stops_=0;
    uint8_t rx_shift_=0, rx_bits_=0, rx_parity_=0, rx_stops_=0, rx_errors_=0, rx_start_remaining_=0;
    RxPhase rx_phase_=RxPhase::idle;
    bool tx_active_=false, tx_parity_sent_=false, rx_parity_sampled_=false;
    bool tx_line_=true, rx_line_=true, rx_wait_high_=false;
    bool irq_asserted_=false, status_read_=false, pending_overrun_=false;
    bool cts_high_=false, dcd_high_=false, dcd_latched_=false;
    bool first_master_reset_=true, rts_reset_high_=true;
    std::array<InputFrame,input_capacity> input_{};
    size_t input_head_=0, input_count_=0;
    uint64_t wire_next_=never, queued_end_=0;
    uint8_t wire_slot_=0, wire_parity_=0;
    uint64_t input_drops_=0, late_inputs_=0;
};

#endif
