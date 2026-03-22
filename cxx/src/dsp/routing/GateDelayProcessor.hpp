/**
 * @file GateDelayProcessor.hpp
 * @brief Gate delay / pulse shaper — delays note-on gate by a fixed time.
 *
 * Type name: GATE_DELAY
 *
 * Ports:
 *   gate_in   IN  lifecycle (driven by VoiceContext note_on/note_off)
 *   gate_in_b IN  non-lifecycle wirable OR input (M-172 INPUT B)
 *   gate_out  OUT unipolar [0,1]
 *
 * Parameters:
 *   delay_time (0.0–6.0 s) — time from rising gate edge to gate_out rising edge
 *   gate_time  (0.0–6.0 s) — output pulse width; 0 = mirror input gate duration
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
        state_              = State::Idle;
        delay_samples_left_ = 0;
        pulse_samples_left_ = 0;
        prev_gate_in_b_     = 0.0f;
        gate_in_b_cv_       = 0.0f;
    }

    bool apply_parameter(const std::string& name, float value) override;

    void inject_cv(std::string_view port, std::span<const float> data) override {
        if (port == "gate_in_b") gate_in_b_cv_ = data.empty() ? 0.0f : data[0];
    }

    void on_note_on(double frequency) override;
    void on_note_off() override;

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    void trigger();  ///< Common rising-edge handler for gate_in and gate_in_b.

    enum class State { Idle, Counting, High, Pulsing };

    int   sample_rate_;
    SmoothedParam delay_time_{0.0f};
    SmoothedParam gate_time_{0.0f};

    State state_              = State::Idle;
    int   delay_samples_left_ = 0;
    int   pulse_samples_left_ = 0;

    // gate_in_b edge detection
    float gate_in_b_cv_   = 0.0f;
    float prev_gate_in_b_ = 0.0f;

    // lifecycle gate state (set by on_note_on/off)
    bool lifecycle_gate_high_ = false;
};

} // namespace audio

#endif // GATE_DELAY_PROCESSOR_HPP
