/**
 * @file GateDelayProcessor.hpp
 * @brief Gate delay / pulse shaper — delays note-on gate by a fixed time.
 *
 * Type name: GATE_DELAY
 *
 * When a note_on event arrives, GATE_DELAY suppresses its gate_out for
 * delay_time seconds then raises it high for the duration of the note.
 * If the note ends before delay_time elapses, gate_out never fires.
 * On note_off, gate_out drops immediately (no trailing pulse).
 *
 * Ports (PORT_CONTROL):
 *   gate_out OUT unipolar [0,1]
 *
 * Parameters:
 *   delay_time (0.0 – 2.0 s, default 0.0) — time from note_on before gate fires
 */

#ifndef GATE_DELAY_PROCESSOR_HPP
#define GATE_DELAY_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>

namespace audio {

class GateDelayProcessor : public Processor {
public:
    explicit GateDelayProcessor(int sample_rate);

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override {
        state_        = State::Idle;
        samples_left_ = 0;
    }

    bool apply_parameter(const std::string& name, float value) override;

    void on_note_on(double frequency) override;
    void on_note_off() override;

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    enum class State { Idle, Counting, High };

    int   sample_rate_;
    SmoothedParam delay_time_{0.0f};
    State state_        = State::Idle;
    int   samples_left_ = 0;
};

} // namespace audio

#endif // GATE_DELAY_PROCESSOR_HPP
