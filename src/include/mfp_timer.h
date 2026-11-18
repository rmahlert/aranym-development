/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_MFP_TIMER_H
#define ARANYM_MFP_TIMER_H

#include <cstdint>

class MFP_Timer {
    std::uint8_t control_ = 0;
    std::uint8_t reload_ = 0;
    std::uint16_t counter_ = 256;
    bool output_ = false;
    bool input_active_ = false;
    std::uint64_t last_ns_ = 0;
    std::uint64_t fraction_ = 0;
    unsigned prescale_phase_ = 0;

    unsigned divisor() const;
    std::uint64_t consume(std::uint64_t ticks);

public:
    explicit MFP_Timer(int = 0) {}
    void reset(std::uint64_t now_ns = 0);
    bool isRunning() const { return control_ != 0; }
    void setControl(std::uint8_t value);
    std::uint8_t getControl() const { return control_; }
    void setData(std::uint8_t value);
    std::uint8_t getData() const { return std::uint8_t(counter_); }
    int compute_timer_freq() const;
    std::uint64_t advance_to(std::uint64_t now_ns);
    std::uint64_t next_deadline_ns() const;
    std::uint64_t set_input(bool active);
};

#endif
