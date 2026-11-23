/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "midi_acia6850.h"

#include <algorithm>

MidiAcia6850::MidiAcia6850(void *context, Transmit transmit, Interrupt irq)
    : context_(context), transmit_(transmit), interrupt_(irq)
{
}

MidiAcia6850::Format MidiAcia6850::format(uint8_t control)
{
    static const Format formats[8] = {
        {7,1,2},{7,2,2},{7,1,1},{7,2,1},
        {8,0,2},{8,0,1},{8,1,1},{8,2,1}
    };
    return formats[(control>>2)&7];
}

uint64_t MidiAcia6850::bit_period(uint8_t control)
{
    static const uint16_t divisors[4]={1,16,64,0};
    return uint64_t(divisors[control&3])*2000;
}

uint64_t MidiAcia6850::add_time(uint64_t value, uint64_t delta)
{
    return value>=never-delta ? never : value+delta;
}

void MidiAcia6850::clear_device(uint64_t now, uint8_t control)
{
    control_=control;
    status_=transmit_empty;
    tdr_=rdr_=tx_shift_=tx_value_=tx_bits_=tx_parity_=tx_stops_=0;
    rx_shift_=rx_bits_=rx_parity_=rx_stops_=rx_errors_=rx_start_remaining_=0;
    tx_active_=tx_parity_sent_=rx_parity_sampled_=false;
    rx_phase_=RxPhase::idle;
    tx_next_=rx_next_=never;tx_origin_=now;
    tx_line_=true;rx_wait_high_=false;
    status_read_=pending_overrun_=dcd_latched_=false;
    update_irq();
}

void MidiAcia6850::reset(uint64_t now)
{
    now_=now;
    input_head_=input_count_=0;
    wire_next_=never;queued_end_=now;
    wire_slot_=wire_parity_=0;rx_line_=true;
    input_drops_=late_inputs_=0;
    first_master_reset_=rts_reset_high_=true;
    clear_device(now,3);
}

void MidiAcia6850::update_irq()
{
    bool asserted=false;
    if ((control_&3)!=3) {
        asserted=((control_&0x80) &&
                  ((status_&(receive_full|overrun)) || pending_overrun_ || dcd_latched_)) ||
                 (((control_>>5)&3)==1 && (status_&transmit_empty) && !cts_high_);
    }
    if (asserted) status_|=interrupt;else status_&=~interrupt;
    if (asserted!=irq_asserted_) {
        irq_asserted_=asserted;
        if (interrupt_) interrupt_(context_,asserted);
    }
}

bool MidiAcia6850::rts_high() const
{
    return rts_reset_high_ || ((control_>>5)&3)==2;
}

uint8_t MidiAcia6850::read_status(uint64_t now)
{
    advance(now);status_read_=dcd_latched_;
    uint8_t result=status_;
    if (cts_high_) result=(result&~transmit_empty)|clear_to_send;
    if (dcd_high_||dcd_latched_) result|=carrier_detect;
    return result;
}

uint8_t MidiAcia6850::read_data(uint64_t now)
{
    advance(now);
    const uint8_t result=rdr_;
    status_&=~(receive_full|parity_error|overrun);
    if (status_read_) dcd_latched_=false;
    status_read_=false;
    // The unread valid character precedes the overrun indication (DS9493R4).
    if (pending_overrun_) {
        status_|=overrun|receive_full;
        pending_overrun_=false;
    }
    update_irq();
    return result;
}

uint64_t MidiAcia6850::next_tx_edge(uint64_t now) const
{
    const uint64_t period=bit_period(control_);
    if (!period) return never;
    const uint64_t remainder=(now-tx_origin_)%period;
    return add_time(now,period-remainder);
}

void MidiAcia6850::write_control(uint8_t value, uint64_t now)
{
    advance(now);
    if ((value&3)==3) {
        rts_reset_high_=first_master_reset_;
        first_master_reset_=false;
        clear_device(now_,value);
        return;
    }
    rts_reset_high_=false;
    const bool changed=(control_&3)!=(value&3);
    control_=value;
    if (changed) {
        tx_origin_=now_;
        if (tx_next_!=never) tx_next_=next_tx_edge(now_);
        if (rx_phase_==RxPhase::start) {
            rx_phase_=RxPhase::idle;
            rx_next_=never;
        }
        else if (rx_phase_!=RxPhase::idle) rx_next_=add_time(now_,bit_period(control_));
    }
    if (!tx_active_) {
        if (!(status_&transmit_empty)||((control_>>5)&3)==3||!tx_line_)
            tx_next_=next_tx_edge(now_);
    }
    if (rx_phase_==RxPhase::idle && !rx_line_) set_receive_line(false,now_);
    update_irq();
}

void MidiAcia6850::write_data(uint8_t value, uint64_t now)
{
    advance(now);tdr_=value;status_&=~transmit_empty;
    if (!tx_active_ && tx_next_==never && !cts_high_ && (control_&3)!=3)
        tx_next_=next_tx_edge(now_);
    update_irq();
}

void MidiAcia6850::begin_transmit()
{
    tx_shift_=tdr_;tx_value_=tx_bits_=tx_parity_=tx_stops_=0;
    status_|=transmit_empty;tx_line_=false;tx_active_=true;tx_parity_sent_=false;
}

void MidiAcia6850::transmit_tick()
{
    const uint64_t period=bit_period(control_);
    if (!period) { tx_next_=never;return; }
    const Format f=format(control_);
    if (tx_active_) {
        // Word, parity and stop selections are not buffered in the MC6850.
        if (tx_bits_<f.bits) {
            const uint8_t bit=tx_shift_&1;tx_line_=bit!=0;
            tx_value_|=bit<<tx_bits_;tx_parity_^=bit;tx_shift_>>=1;++tx_bits_;
        }
        else if (f.parity && !tx_parity_sent_) {
            tx_line_=(tx_parity_^(f.parity==2))!=0;
            tx_parity_sent_=true;
        }
        else if (tx_stops_<f.stops) {tx_line_=true;++tx_stops_;}
        else {
            if (transmit_) transmit_(context_,tx_value_,now_);
            tx_active_=false;
        }
    }
    if (!tx_active_) {
        if (((control_>>5)&3)==3) {tx_line_=false;tx_next_=never;}
        else if (!(status_&transmit_empty) && !cts_high_) begin_transmit();
        else {tx_line_=true;tx_next_=never;}
    }
    if (tx_active_) tx_next_=add_time(now_,period);
    update_irq();
}

void MidiAcia6850::set_receive_line(bool high, uint64_t now)
{
    rx_line_=high;
    if (high) rx_wait_high_=false;
    if (!high && !rx_wait_high_ && rx_phase_==RxPhase::idle && !dcd_high_ && (control_&3)!=3) {
        rx_phase_=RxPhase::start;
        const uint64_t period=bit_period(control_);
        rx_start_remaining_=uint8_t(period/4000);
        if (rx_start_remaining_) rx_next_=add_time(now,2000-now%2000);
        else {rx_start_remaining_=1;rx_next_=add_time(now,period/2);}
    }
}

void MidiAcia6850::receive_line(bool high, uint64_t now)
{
    if (now>now_) advance(now-1);
    if (now<now_) now=now_;
    now_=now;
    set_receive_line(high,now);
    advance(now);
}

void MidiAcia6850::receive_complete()
{
    if (status_&receive_full) pending_overrun_=true;
    else {
        rdr_=rx_shift_;
        status_=(status_&~(framing_error|parity_error))|receive_full|rx_errors_;
    }
    rx_phase_=RxPhase::idle;rx_next_=never;
    rx_wait_high_=!rx_line_;
    update_irq();
}

void MidiAcia6850::sample_receive()
{
    const Format f=format(control_);
    const uint64_t period=bit_period(control_);
    switch (rx_phase_) {
    case RxPhase::idle:rx_next_=never;return;
    case RxPhase::start:
        if (rx_line_) {rx_phase_=RxPhase::idle;rx_next_=never;return;}
        if (--rx_start_remaining_) {rx_next_=add_time(now_,2000);return;}
        rx_shift_=rx_bits_=rx_parity_=rx_stops_=rx_errors_=0;
        rx_parity_sampled_=false;
        rx_phase_=RxPhase::active;
        break;
    case RxPhase::active:
        if (rx_bits_<f.bits) {
            if (rx_line_) rx_shift_|=uint8_t(1u<<rx_bits_);
            rx_parity_^=rx_line_;++rx_bits_;
        }
        else if (f.parity && !rx_parity_sampled_) {
            if (rx_line_!=(bool(rx_parity_)^(f.parity==2))) rx_errors_|=parity_error;
            rx_parity_sampled_=true;
        }
        else {
            if (rx_stops_==0 && !rx_line_) rx_errors_|=framing_error;
            if (++rx_stops_>=f.stops) {receive_complete();return;}
        }
        break;
    }
    rx_next_=add_time(now_,period);
}

void MidiAcia6850::modem_lines(bool cts_high, bool dcd_high, uint64_t now)
{
    advance(now);
    if (!dcd_high_&&dcd_high) dcd_latched_=true;
    cts_high_=cts_high;dcd_high_=dcd_high;
    if (dcd_high) {
        rx_phase_=RxPhase::idle;rx_next_=never;
        pending_overrun_=false;
        status_&=~(receive_full|parity_error|overrun);
    }
    else if (rx_phase_==RxPhase::idle && !rx_line_) set_receive_line(false,now_);
    if (!cts_high && !tx_active_ && !(status_&transmit_empty) && tx_next_==never)
        tx_next_=next_tx_edge(now_);
    update_irq();
}

void MidiAcia6850::resume_host_receive(uint64_t now)
{
    const uint64_t deadline=std::min(wire_next_,rx_next_);
    if (deadline>=now) return;
    const uint64_t overdue=now-deadline;
    const uint64_t period=bit_period(0x15);
    if (overdue<=period) return;
    const uint64_t delay=overdue-period;
    wire_next_=add_time(wire_next_,delay);
    rx_next_=add_time(rx_next_,delay);
}

bool MidiAcia6850::queue_receive(uint8_t value, uint64_t start, uint8_t wire_format, uint8_t errors)
{
    if (input_count_==input_capacity) {++input_drops_;return false;}
    if (start<now_) {start=now_;++late_inputs_;}
    start=std::max(start,queued_end_);
    const Format f=format(wire_format);
    const uint64_t period=bit_period(wire_format);
    if (!period) return false;
    const uint64_t end=add_time(start,period*(1+f.bits+(f.parity!=0)+f.stops));
    if (end==never) {++input_drops_;return false;}
    input_[(input_head_+input_count_)%input_capacity]={start,value,wire_format,errors};
    queued_end_=end;
    if (input_count_++==0) {wire_slot_=0;wire_next_=start;}
    return true;
}

void MidiAcia6850::wire_tick()
{
    const InputFrame &frame=input_[input_head_];
    const Format f=format(frame.format);
    const uint64_t period=bit_period(frame.format);
    const unsigned parity_slot=1+f.bits,stop_slot=parity_slot+(f.parity!=0);
    bool high=true;
    if (wire_slot_==0) {high=false;wire_parity_=0;}
    else if (wire_slot_<=f.bits) {
        high=(frame.value>>(wire_slot_-1))&1;
        wire_parity_^=high;
    }
    else if (f.parity && wire_slot_==parity_slot) {
        high=bool(wire_parity_)^(f.parity==2);
        if (frame.errors&parity_error) high=!high;
    }
    else if (wire_slot_==stop_slot && (frame.errors&framing_error)) high=false;
    set_receive_line(high,now_);
    if (++wire_slot_>stop_slot+f.stops) {
        input_head_=(input_head_+1)%input_capacity;
        if (--input_count_) {
            const InputFrame &next=input_[input_head_];
            wire_slot_=0;wire_next_=std::max(now_,next.start);
        }
        else {wire_next_=never;queued_end_=now_;}
    }
    else wire_next_=add_time(now_,period);
}

uint64_t MidiAcia6850::next_deadline_ns() const
{
    return std::min(wire_next_,std::min(tx_next_,rx_next_));
}

void MidiAcia6850::advance(uint64_t now)
{
    if (now<now_) return;
    for (;;) {
        const uint64_t deadline=next_deadline_ns();
        if (deadline==never || deadline>now) break;
        now_=deadline;
        if (wire_next_==deadline) wire_tick();
        if (rx_next_==deadline) sample_receive();
        if (tx_next_==deadline) transmit_tick();
    }
    now_=now;
}
